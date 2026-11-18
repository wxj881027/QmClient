#ifndef ENGINE_RUST_H
#define ENGINE_RUST_H
#include "console.h"

#include <new>

typedef IConsole::IResult IConsole_IResult;
typedef IConsole::FCommandCallback IConsole_FCommandCallback;

inline bool TryRegisterRustConsoleCommand(IConsole &Console, const char *pName, const char *pParams, int Flags, IConsole_FCommandCallback pfnFunc, void *pUser, const char *pHelp) noexcept
{
	try
	{
		Console.Register(pName, pParams, Flags, pfnFunc, pUser, pHelp);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
}
#endif // ENGINE_RUST_H
