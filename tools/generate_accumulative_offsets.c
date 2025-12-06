#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <locale.h>

#define HANZI_START 0x4E00
#define HANZI_END 0x9FFF

// 检查字符是否为汉字
bool is_hanzi(int unicode) {
    return (unicode >= HANZI_START && unicode <= HANZI_END);
}

// 将UTF-8字节序列转换为Unicode码点
int utf8_to_unicode(unsigned char* bytes, int* byte_count) {
    *byte_count = 0;
    
    if ((bytes[0] & 0x80) == 0) {
        // ASCII字符 (0xxxxxxx)
        *byte_count = 1;
        return bytes[0];
    } else if ((bytes[0] & 0xE0) == 0xC0) {
        // 2字节字符 (110xxxxx 10xxxxxx)
        if ((bytes[1] & 0xC0) != 0x80) return -1; // 验证后续字节
        *byte_count = 2;
        return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    } else if ((bytes[0] & 0xF0) == 0xE0) {
        // 3字节字符 (1110xxxx 10xxxxxx 10xxxxxx)
        if ((bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80) return -1;
        *byte_count = 3;
        return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
    } else if ((bytes[0] & 0xF8) == 0xF0) {
        // 4字节字符 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if ((bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80) return -1;
        *byte_count = 4;
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | 
               ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
    }
    
    return -1; // 无效的UTF-8序列
}

int main(int argc, char *argv[]) {
    // 设置本地化以便正确处理UTF-8
    setlocale(LC_ALL, "");
    
    if (argc != 3) {
        printf("用法: %s <输入文件> <输出文件>\n", argv[0]);
        printf("说明: 从文本文件中提取汉字，生成ImGui格式的累积偏移数组\n");
        return 1;
    }
    
    FILE *input_file = fopen(argv[1], "rb");
    if (!input_file) {
        perror("无法打开输入文件");
        return 1;
    }
    
    // 创建一个布尔数组来标记哪些汉字出现过
    bool hanzi_present[HANZI_END - HANZI_START + 1] = {false};
    
    // 读取文件内容并识别汉字
    unsigned char buffer[8];
    int buffer_pos = 0;
    int total_chars = 0;
    int detected_hanzi_count = 0;
    
    int ch;
    while ((ch = fgetc(input_file)) != EOF) {
        buffer[buffer_pos++] = (unsigned char)ch;
        
        // 尝试解析UTF-8字符（尝试不同长度）
        for (int len = 1; len <= buffer_pos && len <= 4; len++) {
            int byte_count;
            int unicode = utf8_to_unicode(&buffer[buffer_pos - len], &byte_count);
            
            // 如果成功解析出一个字符
            if (byte_count == len) {
                total_chars++;
                if (is_hanzi(unicode)) {
                    hanzi_present[unicode - HANZI_START] = true;
                    detected_hanzi_count++;
                }
                
                // 移动剩余未处理的字节到缓冲区开始
                buffer_pos -= len;
                for (int i = 0; i < buffer_pos; i++) {
                    buffer[i] = buffer[i + len];
                }
                break; // 成功解析后跳出循环
            }
        }
        
        // 如果缓冲区满了但还没处理完，重置
        if (buffer_pos >= 4) {
            buffer_pos = 0;
        }
    }
    
    printf("总共处理了 %d 个字符，其中找到 %d 个汉字\n", total_chars, detected_hanzi_count);
    
    fclose(input_file);
    
    // 如果没有找到汉字，退出
    if (detected_hanzi_count == 0) {
        printf("未在输入文件中找到汉字\n");
        return 1;
    }
    
    // 收集所有出现过的汉字码点
    int hanzi_codes[HANZI_END - HANZI_START + 1]; // 最多这么多汉字
    int hanzi_count = 0;
    
    for (int i = 0; i <= (HANZI_END - HANZI_START); i++) {
        if (hanzi_present[i]) {
            hanzi_codes[hanzi_count++] = HANZI_START + i;
        }
    }
    
    // 生成累积偏移数组
    short* offsets = malloc(hanzi_count * sizeof(short));
    if (!offsets) {
        printf("内存分配失败\n");
        return 1;
    }
    
    // 第一个偏移是0
    offsets[0] = 0;
    
    // 后续偏移是当前字符与前一字符的差值
    for (int i = 1; i < hanzi_count; i++) {
        int diff = hanzi_codes[i] - hanzi_codes[i-1];
        // 检查是否超出short范围（理论上不会）
        if (diff > 32767 || diff < -32768) {
            printf("警告：偏移值超出short范围: %d\n", diff);
        }
        offsets[i] = (short)diff;
    }
    
    // 写入输出文件
    FILE *output_file = fopen(argv[2], "w");
    if (!output_file) {
        perror("无法创建输出文件");
        free(offsets);
        return 1;
    }
    
    fprintf(output_file, "// 从文件 %s 提取的汉字累积偏移数组\n", argv[1]);
    fprintf(output_file, "// 格式说明：存储为从初始Unicode码点0x4E00开始的累积偏移\n");
    fprintf(output_file, "static const short accumulative_offsets_from_0x4E00[%d] = {\n", hanzi_count);
    
    for (int i = 0; i < hanzi_count; i++) {
        if (i % 16 == 0) {
            fprintf(output_file, "    ");
        }
        fprintf(output_file, "%d", offsets[i]);
        if (i < hanzi_count - 1) {
            fprintf(output_file, ",");
        }
        if ((i + 1) % 16 == 0 || i == hanzi_count - 1) {
            fprintf(output_file, "\n");
        } else {
            fprintf(output_file, " ");
        }
    }
    fprintf(output_file, "};\n");
    
    fclose(output_file);
    free(offsets);
    
    printf("成功生成包含 %d 个汉字的累积偏移数组到文件 %s\n", hanzi_count, argv[2]);
    printf("第一个汉字: U+%04X, 最后一个汉字: U+%04X\n", 
           hanzi_codes[0], hanzi_codes[hanzi_count-1]);
    
    return 0;
}