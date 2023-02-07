#include <windows.h>
#include <stdio.h>
#include "LdrDllNotificationHook.h"

static VOID CALLBACK TestNotificationFunction(
	_In_     ULONG                       NotificationReason,
	_In_     PCLDR_DLL_NOTIFICATION_DATA NotificationData,
	_In_opt_ PVOID                       Context
)
{
	PCUNICODE_STRING BaseDllName;
	PVOID hModule;

	switch (NotificationReason)
	{
	case LDR_DLL_NOTIFICATION_REASON_LOADED:
		BaseDllName = NotificationData->Loaded.BaseDllName;
		hModule = NotificationData->Loaded.DllBase;
		break;

	case LDR_DLL_NOTIFICATION_REASON_UNLOADED:
		BaseDllName = NotificationData->Unloaded.BaseDllName;
		hModule = NotificationData->Unloaded.DllBase;
		break;

	default:
		return;
	}

	printf("| %s callback: %p %.*S\n",
		(NotificationReason == LDR_DLL_NOTIFICATION_REASON_LOADED) ? "Load" : "Unload",
		hModule, (unsigned int)(BaseDllName->Length / sizeof(WCHAR)), BaseDllName->Buffer);
	printf("| Context: %p\n", Context);
}

static VOID CALLBACK NotificationFunctionHook(
	_In_     ULONG                       NotificationReason,
	_In_     PCLDR_DLL_NOTIFICATION_DATA NotificationData,
	_In_opt_ PVOID                       Context,
	_In_     PLDR_DLL_NOTIFICATION_FUNCTION OriginalNotificationFunction
)
{
	printf("~ %s callback hook\n",
		(NotificationReason == LDR_DLL_NOTIFICATION_REASON_LOADED) ? "Load" : "Unload");

	if (GetKeyState(VK_SHIFT) < 0)
	{
		printf("~ Holding shift, not calling original callback\n");
	}
	else
	{
		printf("> Not holding shift, calling original callback\n");
		OriginalNotificationFunction(NotificationReason, NotificationData, Context);
		printf("< Original callback called\n");
	}
}

int main()
{
	HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
	if (!hNtdll)
	{
		return 1;
	}

	PLDR_REGISTER_DLL_NOTIFICATION pLdrRegisterDllNotification =
		(PLDR_REGISTER_DLL_NOTIFICATION)GetProcAddress(hNtdll, "LdrRegisterDllNotification");
	if (!pLdrRegisterDllNotification)
	{
		return 1;
	}

	PLDR_UNREGISTER_DLL_NOTIFICATION pLdrUnregisterDllNotification =
		(PLDR_UNREGISTER_DLL_NOTIFICATION)GetProcAddress(hNtdll, "LdrUnregisterDllNotification");
	if (!pLdrUnregisterDllNotification)
	{
		return 1;
	}

	printf("========================================\n");
	printf("Setting dll notification callback\n");
	PVOID cookie;
	pLdrRegisterDllNotification(0, TestNotificationFunction, (PVOID)(UINT_PTR)0xDEADBEEF, &cookie);

	printf("\n========================================\n");
	printf("Loading Ws2_32.dll\n");
	HMODULE hWs2_32 = LoadLibrary(L"ws2_32.dll");

	printf("\n========================================\n");
	printf("Unloading Ws2_32.dll\n");
	FreeLibrary(hWs2_32);

	printf("\n========================================\n");
	printf("Setting dll notification callback hook\n");
	HookLdrDllNotifications(NotificationFunctionHook);

	while (1)
	{
		printf("Press enter, type 'q' to quit... ");
		if (getchar() == 'q')
		{
			break;
		}

		printf("\n========================================\n");
		printf("Loading Ws2_32.dll\n");
		hWs2_32 = LoadLibrary(L"ws2_32.dll");

		printf("Press enter");
		getchar();

		printf("\n========================================\n");
		printf("Unloading Ws2_32.dll\n");
		FreeLibrary(hWs2_32);
	}

	printf("\n========================================\n");
	printf("Removing dll notification callback hook\n");
	UnhookLdrDllNotifications();

	printf("\n========================================\n");
	printf("Loading Ws2_32.dll\n");
	hWs2_32 = LoadLibrary(L"ws2_32.dll");

	printf("\n========================================\n");
	printf("Unloading Ws2_32.dll\n");
	FreeLibrary(hWs2_32);
}
