#ifndef __PR_MODULE_HPP__
#define __PR_MODULE_HPP__

#ifdef __linux__
#define DLLEXPORT __attribute__((visibility("default")))
#else
#define DLLEXPORT __declspec(dllexport)
#endif

#endif
