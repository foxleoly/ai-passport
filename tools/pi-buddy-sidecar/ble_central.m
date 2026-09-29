#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>
#import <pthread.h>
#include <string.h>

// Event kinds returned by pb_poll.
enum { PB_EV_STATE = 1, PB_EV_CONNECTED = 2, PB_EV_DISCONNECTED = 3, PB_EV_TXDATA = 4, PB_EV_DISCOVERED = 5 };

// NUS 128-bit UUIDs.
static NSString * const NUS_SERVICE_UUID = @"6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static NSString * const NUS_RX_UUID      = @"6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static NSString * const NUS_TX_UUID      = @"6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

#define PB_EV_CAP 600

@interface PBCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property(nonatomic, strong) CBCentralManager *mgr;
@property(nonatomic, strong) CBPeripheral *periph;
@property(nonatomic, strong) CBCharacteristic *rx;
@property(nonatomic, strong) CBCharacteristic *tx;
@end

// Global single-instance bridge (the sidecar drives one device).
static PBCentral *g_c = nil;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_ev_kind[4];
static int g_ev_state[4];
static uint8_t g_ev_data[4][PB_EV_CAP];
static int g_ev_len[4];
static int g_head, g_tail, g_count;
static dispatch_semaphore_t g_sem;

static void pb_push(int kind, int state, const uint8_t *data, int len) {
    pthread_mutex_lock(&g_lock);
    if (g_count < 4) {
        int i = g_tail;
        g_ev_kind[i] = kind;
        g_ev_state[i] = state;
        if (data && len > PB_EV_CAP) len = PB_EV_CAP;
        if (data && len > 0) memcpy(g_ev_data[i], data, len);
        g_ev_len[i] = len;
        g_tail = (g_tail + 1) & 3;
        g_count++;
    }
    pthread_mutex_unlock(&g_lock);
    dispatch_semaphore_signal(g_sem);
}

@implementation PBCentral

- (instancetype)init {
    self = [super init];
    if (self) {
        g_sem = dispatch_semaphore_create(0);
        _mgr = [[CBCentralManager alloc] initWithDelegate:self
                queue:dispatch_queue_create("pibud.cb", NULL)];
    }
    return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    pb_push(PB_EV_STATE, (int)central.state, nil, 0);
}

- (void)centralManager:(CBCentralManager *)central didDiscoverPeripheral:(CBPeripheral *)p
   advertiserRSSI:(NSNumber *)rssi advertisedData:(NSDictionary *)ad {
    NSString *name = [ad objectForKey:CBAdvertisementDataLocalNameKey];
    pb_push(PB_EV_DISCOVERED, 0, (const uint8_t *)(name ? name.UTF8String : ""),
            (int)strlen(name ? name.UTF8String : ""));
    if (self.periph) return;
    if (![name hasPrefix:@"Pi-"]) return; // keep scanning; only our buddy devices
    [central stopScan];
    self.periph = p;
    [p setDelegate:self];
    [central connectPeripheral:p options:nil];
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)p {
    [p discoverServices:@[[CBUUID UUIDWithString:NUS_SERVICE_UUID]]];
}

- (void)centralManager:(CBCentralManager *)central didDisconnectPeripheral:(CBPeripheral *)p
     error:(NSError *)error {
    self.periph = nil;
    self.rx = nil;
    self.tx = nil;
    pb_push(PB_EV_DISCONNECTED, 0, nil, 0);
}

- (void)peripheral:(CBPeripheral *)p didDiscoverServices:(NSError *)error {
    for (CBService *s in p.services) {
        if ([s.UUID isEqual:[CBUUID UUIDWithString:NUS_SERVICE_UUID]]) {
            [p discoverCharacteristics:@[
                    [CBUUID UUIDWithString:NUS_RX_UUID],
                    [CBUUID UUIDWithString:NUS_TX_UUID]]
               forService:s];
            break;
        }
    }
}

- (void)peripheral:(CBPeripheral *)p didDiscoverCharacteristicsForService:(CBService *)s
       error:(NSError *)error {
    for (CBCharacteristic *c in s.characteristics) {
        if ([c.UUID isEqual:[CBUUID UUIDWithString:NUS_RX_UUID]]) self.rx = c;
        if ([c.UUID isEqual:[CBUUID UUIDWithString:NUS_TX_UUID]]) {
            self.tx = c;
            [p setNotifyValue:YES forCharacteristic:c];
            [p readValueForCharacteristic:c];
        }
    }
    pb_push(PB_EV_CONNECTED, 0, nil, 0);
}

- (void)peripheral:(CBPeripheral *)p didUpdateValueForCharacteristic:(CBCharacteristic *)c
       error:(NSError *)error {
    if (c == self.tx && c.value.length > 0) {
        pb_push(PB_EV_TXDATA, 0, (const uint8_t *)c.value.bytes, (int)c.value.length);
    }
}

@end

// ---- C wrappers (pull model) ----
void *pb_new(void) {
    if (!g_c) g_c = [[PBCentral alloc] init];
    return g_c;
}

int pb_scan(void *c) {
    if (g_c.mgr.state != CBManagerStatePoweredOn) return -1;
    // Scan all, filter client-side by advertised name prefix "Pi-" (NUS may
    // advertise only the 16-bit 0xF0DE UUID, so a 128-bit filter would miss it).
    // allowDuplicates: the macOS shared scanner dedupes devices it already knows
    // (system DB / GUI); without this, a previously-seen buddy is never
    // re-reported to third-party sessions. The raw key string is used because the
    // new SDK dropped the CBScanOption* constants.
    [g_c.mgr scanForPeripheralsWithServices:nil
        options:@{ @"CBScanOptionAllowDuplicates": @YES }];
    return 0;
}

void pb_write_rx(void *c, const uint8_t *data, int len) {
    if (!g_c.rx || !g_c.periph || len <= 0) return;
    NSData *d = [NSData dataWithBytes:data length:(NSUInteger)len];
    [g_c.periph writeValue:d forCharacteristic:g_c.rx type:CBCharacteristicWriteWithResponse];
}

int pb_state(void *c) {
    return (int)g_c.mgr.state;
}

// Block up to timeout_ms for the next event. Returns 1 (event), 0 (timeout), -1 (error).
int pb_poll(void *c, int *kind, int *state, uint8_t *data, int cap, int *datalen, int timeout_ms) {
    *kind = 0; *state = 0; *datalen = 0;
    long wait = (long)(timeout_ms * 1000);
    if (dispatch_semaphore_wait(g_sem, dispatch_time(DISPATCH_TIME_NOW, wait)) != 0) {
        return 0; // timeout
    }
    pthread_mutex_lock(&g_lock);
    if (g_count == 0) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    int i = g_head;
    *kind = g_ev_kind[i];
    *state = g_ev_state[i];
    int n = g_ev_len[i];
    if (data && n > cap) n = cap;
    if (data && n > 0) memcpy(data, g_ev_data[i], n);
    *datalen = n;
    g_head = (g_head + 1) & 3;
    g_count--;
    pthread_mutex_unlock(&g_lock);
    return 1;
}

void pb_release(void *c) {
    if (g_c) {
        [g_c release];
        g_c = nil;
    }
}
