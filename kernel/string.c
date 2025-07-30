#include "types.h"

// 将 dst 指向的内存区域的前 n 个字节设置为 c。
void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

// 比较 v1 和 v2 指向的内存区域的前 n 个字节。
// 返回值: <0 (v1 < v2), 0 (v1 == v2), >0 (v1 > v2)
int
memcmp(const void *v1, const void *v2, uint n)
{
  const uchar *s1, *s2;

  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

// 从 src 复制 n 个字节到 dst。
// 可以处理源和目标内存区域重叠的情况。
void*
memmove(void *dst, const void *src, uint n)
{
  const char *s;
  char *d;

  if(n == 0)
    return dst;
  
  s = src;
  d = dst;
  // 当源地址在目标地址之前，并且两个区域有重叠时，
  // 从后向前复制以避免覆盖尚未复制的数据。
  if(s < d && s + n > d){
    s += n;
    d += n;
    while(n-- > 0)
      *--d = *--s;
  } else // 否则，从前向后复制即可。
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

// memcpy 是为了安抚 GCC 编译器而存在的。实际应使用 memmove。
// 在xv6中，memcpy直接调用memmove，因为memmove更安全。
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

// 比较字符串 p 和 q 的前 n 个字节。
int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

// 从 t 复制最多 n-1 个字符到 s。
// 如果 t 的长度小于 n，则用 NULL 字符填充 s 的剩余部分。
// 如果 t 的长度大于等于 n，则结果字符串不会以 NULL 结尾。
char*
strncpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  // 如果 t 已经复制完毕但 n 还有剩余，用0填充
  while(n-- > 0)
    *s++ = 0;
  return os;
}

// 类似于 strncpy，但保证结果字符串以 NULL 结尾。
// 这是更安全的版本。
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;

  os = s;
  if(n <= 0)
    return os;
  // 留出一个字节给结尾的 NULL
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  // 添加 NULL 结尾
  *s = 0;
  return os;
}

// 计算字符串 s 的长度。
int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}
