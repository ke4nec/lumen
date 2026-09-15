// stb_truetype 的唯一实现点。移动端与桌面系统字体管理器都只引用
// 声明，避免静态库链接时出现重复定义（MSVC LNK4006）。
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
