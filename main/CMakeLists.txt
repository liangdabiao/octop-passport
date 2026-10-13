idf_component_register(
    SRCS "main.c"
         # 应用
         "op_app.c"
         "op_ui.c"
         # 纯逻辑：与 ESP-IDF/LVGL 解耦，宿主测试直接编译这些文件
         "op_pager.c"
         "op_wire.c"
         "op_ws_proto.c"
         # 服务与网络（op_wifi.cc 是 esp-wifi-connect 组件的 C 封装）
         "op_store.c"
         "op_net.c"
         "op_voice.c"
         "op_ws.c"
         "op_octop.c"
         "op_wifi.cc"
         # 串口截屏（FAP_SCREENSHOT_V1，家族发布/核对工具）
         "fap_screenshot.c"
    INCLUDE_DIRS "."
    # 联网对话终端：WiFi/网络栈 + esp-wifi-connect 配网组件 + JSON。
    # BLE 不初始化（家族基线：BT 关闭）。基线 demo 源码留在 main/ 仅作参考，
    # 不参与本应用构建；音频 P-M2 录音时再接。
    REQUIRES bsp esp_event esp_hw_support esp_timer nvs_flash esp_wifi
             esp_driver_usb_serial_jtag
             esp_netif json lwip esp-wifi-connect
)

# 中文字库：op_font_16 收 GB2312 全部常用字（约 3755 字，astock-quote 同源，
# 服务器返回的文章是动态文本，UI 子集会缺字形）。按官方推荐方式作为 main
# 组件的额外源文件编译；只用这一条路径，不要重复加进 SRCS。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/op_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/op_font_24.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/op_font_32.c"
)
