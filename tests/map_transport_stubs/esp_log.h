#pragma once
static inline void test_map_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI(tag, ...) test_map_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_map_log(tag, __VA_ARGS__)
