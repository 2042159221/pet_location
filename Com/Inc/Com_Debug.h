/**
 * @file Com_Debug.h
 * @author Ming
 * @date 2026-04-02
 * @version 1.0
 * @brief
 * This file contains the function prototypes for the debugging utilities.
 */
#ifndef COM_DEBUG_H
#define COM_DEBUG_H
#include "usart.h"
#include "stdio.h"
#include "string.h"
#define DEVELOPMENT

// DEV MDOE
#ifdef DEVELOPMENT
#define FILENAME strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__
//不带换行功能
#define COM_DEBUG(format, ...)  printf("[%s:%d]" format, FILENAME, __LINE__, ##__VA_ARGS__)
//带换行功能
#define COM_DEBUG_LN(format, ...)  printf("[%s:%d]" format "\r\n", FILENAME, __LINE__, ##__VA_ARGS__)

#else
// Release mode
#define COM_DEBUG(format, ...)
#define COM_DEBUG_LN(format, ...)
#endif

/**
 * @file Com_Debug.h
 * @brief description
 */
int fputc(int ch, FILE *f);


#endif /* COM_DEBUG_H */
