idf_component_register(
    SRCS "pibud_app.c"
         "pibud_ui.c"
         "pibud_state.c"
         "pibud_protocol.c"
         "pibud_text_layout.c"
         "pibud_i4.c"
         "pibud_line.c"
         "pibud_form.c"
         "pibud_prov_html.c"
         "pibud_token.c"
         "pibud_usbc.c"
         "pibud_ws.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_http_server esp_netif esp_timer esp_wifi
             json nvs_flash esp_websocket_client mdns
)
