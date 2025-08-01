#include "types.h"

/**
 * @brief 将指定内存区域的前 n 个字节设置为指定的值。
 * @param dst 指向要填充的内存区域的指针。
 * @param c   要设置的值。该值以 int 类型传入，但函数在填充内存时会使用其无符号字符的表示。
 * @param n   要设置的字节数。
 * @return    返回一个指向内存区域 dst 的指针。
 */
void*
memset(void *dst, int c, uint n)
{
  // 将通用指针 dst 转换为字符指针 cdst，以便按字节进行操作。
  char *cdst = (char *) dst;
  int i;
  // 循环 n 次，将每个字节都设置为 c。
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  // 返回原始指针，方便链式调用。
  return dst;
}

/**
 * @brief 比较两个内存区域的前 n 个字节。
 * @param v1  指向第一个内存区域的指针。
 * @param v2  指向第二个内存区域的指针。
 * @param n   要比较的字节数。
 * @return    一个整数，表示比较结果：
 *            < 0 表示 v1 小于 v2。
 *            = 0 表示 v1 等于 v2。
 *            > 0 表示 v1 大于 v2。
 */
int
memcmp(const void *v1, const void *v2, uint n)
{
  // 将通用指针转换为无符号字符指针，以便按字节进行比较。
  // 使用 uchar 是为了确保比较的是正值，避免 char 的符号扩展问题。
  const uchar *s1, *s2;

  s1 = v1;
  s2 = v2;
  // 循环 n 次，逐字节比较。
  while(n-- > 0){
    // 如果发现不相等的字节，则返回它们的差值。
    if(*s1 != *s2)
      return *s1 - *s2;
    // 如果相等，则继续比较下一个字节。
    s1++, s2++;
  }

  // 如果 n 个字节都相等，则返回 0。
  return 0;
}

/**
 * @brief 从源地址 src 复制 n 个字节到目标地址 dst。
 *        这个函数能够正确处理源和目标内存区域重叠的情况。
 * @param dst 目标内存区域的指针。
 * @param src 源内存区域的指针。
 * @param n   要复制的字节数。
 * @return    返回一个指向目标内存区域 dst 的指针。
 */
void*
memmove(void *dst, const void *src, uint n)
{
  const char *s;
  char *d;

  // 如果要复制的字节数为 0，则直接返回。
  if(n == 0)
    return dst;
  
  s = src;
  d = dst;
  
  // 关键逻辑：处理内存重叠
  // 当源地址在目标地址之前 (s < d)，并且源的末尾 (s+n) 超过了目标的开头 (d) 时，
  // 说明发生了重叠，且从前向后复制会覆盖尚未读取的源数据。
  // 此时必须从后向前复制。
  if(s < d && s + n > d){
    // 将指针移动到源和目标区域的末尾。
    s += n;
    d += n;
    // 从后向前复制 n 个字节。
    while(n-- > 0)
      *--d = *--s;
  } else {
    // 如果没有重叠，或者重叠情况允许从前向后复制
    // (例如，目标地址在源地址之前)，则直接从前向后复制。
    while(n-- > 0)
      *d++ = *s++;
  }

  // 返回原始目标指针。
  return dst;
}

// memcpy 是为了安抚 GCC 编译器而存在的。在 xv6 内核中，我们总是使用 memmove，
// 因为它更安全，可以处理内存重叠的情况。这是一个很好的内核编程实践。
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

/**
 * @brief 比较两个字符串的前 n 个字节。
 * @param p   指向第一个字符串的指针。
 * @param q   指向第二个字符串的指针。
 * @param n   要比较的最大字节数。
 * @return    一个整数，表示比较结果：
 *            < 0 表示 p 小于 q。
 *            = 0 表示 p 等于 q。
 *            > 0 表示 p 大于 q。
 */
int
strncmp(const char *p, const char *q, uint n)
{
  // 循环直到 n 为 0，或者遇到字符串结束符 '\0'，或者发现不相等的字符。
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  // 如果是因为 n 减到了 0 (意味着前 n 个字符都相等)，则返回 0。
  if(n == 0)
    return 0;
  // 否则，返回第一个不相等字符的差值。
  // 转换为 uchar 是为了防止 char 的符号位影响比较结果。
  return (uchar)*p - (uchar)*q;
}

/**
 * @brief 从字符串 t 复制最多 n 个字符到 s。
 * @warning 这个函数存在风险！如果源字符串 t 的长度大于或等于 n，
 *          那么复制到 s 的结果将不会以空字符 ('\0') 结尾。
 * @param s   目标缓冲区。
 * @param t   源字符串。
 * @param n   要复制的最大字符数（包括可能的空字符）。
 * @return    返回指向目标缓冲区 s 的指针。
 */
char*
strncpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  // 复制字符，直到 n 为 0，或者 t 已经到了结尾 ('\0')。
  // 这个表达式相当紧凑：将 *t 赋值给 *s，然后检查这个值是否为 0。
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  // 如果 t 已经复制完毕但 n 还有剩余，用空字符填充 s 的剩余部分。
  // 这是 strncpy 的一个特性。
  while(n-- > 0)
    *s++ = 0;
  return os;
}

/**
 * @brief 安全地从 t 复制最多 n-1 个字符到 s，并保证结果字符串以空字符 ('\0') 结尾。
 *        这是对 strncpy 的一个更安全的替代方案。
 * @param s   目标缓冲区。
 * @param t   源字符串。
 * @param n   目标缓冲区的总大小。
 * @return    返回指向目标缓冲区 s 的指针。
 */
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  // 如果缓冲区大小无效，则直接返回。
  if(n <= 0)
    return os;
  // 循环复制，但保留一个字节的空间用于存放结尾的空字符。
  // n-1个字符被复制后循环会停止。
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  // 在字符串的末尾强制放置空字符，确保字符串正确终止。
  *s = 0;
  return os;
}

/**
 * @brief 计算字符串的长度，不包括结尾的空字符 ('\0')。
 * @param s   指向要计算长度的字符串的指针。
 * @return    字符串 s 的长度。
 */
int
strlen(const char *s)
{
  int n;

  // 循环遍历字符串，直到遇到空字符 s[n] == '\0'。
  for(n = 0; s[n]; n++)
    ;
  return n;
}
