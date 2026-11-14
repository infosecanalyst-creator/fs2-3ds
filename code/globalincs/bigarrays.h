#pragma once
// 3DS: the biggest game tables live on the heap, not in .bss. Their 15 MB of .bss made the
// .3dsx ask the Homebrew loader for 24 MB at once, and the loader ran out of memory
// (D86007F3) when the game was started from the test agent (Nov 13 2026).
// Zeroed like .bss, then each element constructed, before main().
#include <cstdlib>
#include <new>
template <class T> T *heap_table(size_t n)
{
	T *p = static_cast<T *>(calloc(n, sizeof(T)));
	if (!p) abort();
	for (size_t i = 0; i < n; i++) new (&p[i]) T();
	return p;
}
