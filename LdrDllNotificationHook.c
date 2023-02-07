#include "LdrDllNotificationHook.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

// Reference:
// https://github.com/gmh5225/X64DBG-ViewDllNotification/blob/09b73617635a9da92833544979bd8af31a3bdecb/src/plugin.cpp
typedef struct _DBG_LDRP_DLL_NOTIFICATION_BLOCK {
	LIST_ENTRY Links;
	PLDR_DLL_NOTIFICATION_FUNCTION NotificationFunction;
	PVOID Context;
} DBG_LDRP_DLL_NOTIFICATION_BLOCK, * PDBG_LDRP_DLL_NOTIFICATION_BLOCK;

typedef struct _DLL_NOTIFICATION_HOOK_CONTEXT {
	PLDR_DLL_NOTIFICATION_FUNCTION OriginalNotificationFunction;
	PVOID OriginalContext;
} DLL_NOTIFICATION_HOOK_CONTEXT, * PDLL_NOTIFICATION_HOOK_CONTEXT;

typedef struct _DLL_NOTIFICATION_HOOK_CONTEXT_ARRAY {
	PDLL_NOTIFICATION_HOOK_CONTEXT pItems;         // Data heap
	size_t                         capacity;       // Size of allocated data heap, items
	size_t                         size;           // Actual number of data items
} DLL_NOTIFICATION_HOOK_CONTEXT_ARRAY, * PDLL_NOTIFICATION_HOOK_CONTEXT_ARRAY;

static DBG_LDRP_DLL_NOTIFICATION_BLOCK g_hookDllNotificationBlock;
static DLL_NOTIFICATION_HOOK_CONTEXT_ARRAY g_hookContextArray;
static PLDR_DLL_NOTIFICATION_FUNCTION_HOOK g_hookDllNotification;

static const size_t INITIAL_HOOK_CONTEXT_ARRAY_CAPACITY = 32;

static ptrdiff_t AddHookContext()
{
	if (!g_hookContextArray.pItems)
	{
		g_hookContextArray.capacity = INITIAL_HOOK_CONTEXT_ARRAY_CAPACITY;
		g_hookContextArray.pItems = HeapAlloc(
			GetProcessHeap(), 0, g_hookContextArray.capacity * sizeof(DLL_NOTIFICATION_HOOK_CONTEXT));
		if (!g_hookContextArray.pItems)
		{
			return -1;
		}
	}
	else if (g_hookContextArray.size >= g_hookContextArray.capacity)
	{
		PDLL_NOTIFICATION_HOOK_CONTEXT p = HeapReAlloc(
			GetProcessHeap(), 0, g_hookContextArray.pItems, (g_hookContextArray.capacity * 2) * sizeof(DLL_NOTIFICATION_HOOK_CONTEXT));
		if (!p)
		{
			return -1;
		}

		g_hookContextArray.capacity *= 2;
		g_hookContextArray.pItems = p;
	}

	return g_hookContextArray.size++;
}

static VOID ClearHookContextArray()
{
	HeapFree(GetProcessHeap(), 0, g_hookContextArray.pItems);
	g_hookContextArray.pItems = NULL;
	g_hookContextArray.capacity = 0;
	g_hookContextArray.size = 0;
}

static VOID CALLBACK EmptyNotificationFunction(
	_In_     ULONG                       NotificationReason,
	_In_     PCLDR_DLL_NOTIFICATION_DATA NotificationData,
	_In_opt_ PVOID                       Context
)
{
	// Do nothing.
}

static VOID CALLBACK HookWrapperNotificationFunction(
	_In_     ULONG                       NotificationReason,
	_In_     PCLDR_DLL_NOTIFICATION_DATA NotificationData,
	_In_opt_ PVOID                       Context
)
{
	ptrdiff_t hookContextIndex = (ptrdiff_t)Context;
	PDLL_NOTIFICATION_HOOK_CONTEXT hookContext = &g_hookContextArray.pItems[hookContextIndex];

	g_hookDllNotification(NotificationReason, NotificationData, hookContext->OriginalContext, hookContext->OriginalNotificationFunction);
}

static VOID CALLBACK FirstNotificationFunction(
	_In_     ULONG                       NotificationReason,
	_In_     PCLDR_DLL_NOTIFICATION_DATA NotificationData,
	_In_opt_ PVOID                       Context
)
{
	PLIST_ENTRY head = g_hookDllNotificationBlock.Links.Blink;

	// Reset hooks.

	for (PLIST_ENTRY entry = g_hookDllNotificationBlock.Links.Flink; entry != head; entry = entry->Flink)
	{
		PDBG_LDRP_DLL_NOTIFICATION_BLOCK block = CONTAINING_RECORD(entry, DBG_LDRP_DLL_NOTIFICATION_BLOCK, Links);

		if (block->NotificationFunction == HookWrapperNotificationFunction)
		{
			ptrdiff_t hookContextIndex = (ptrdiff_t)block->Context;
			PDLL_NOTIFICATION_HOOK_CONTEXT hookContext = &g_hookContextArray.pItems[hookContextIndex];
			block->NotificationFunction = hookContext->OriginalNotificationFunction;
			block->Context = hookContext->OriginalContext;
		}
	}

	// Clear array and add hooks again to make sure the hook is applied to new
	// items, and to remove old items from the array and prevent the array from
	// growing infinitely.

	g_hookContextArray.size = 0;

	for (PLIST_ENTRY entry = g_hookDllNotificationBlock.Links.Flink; entry != head; entry = entry->Flink)
	{
		PDBG_LDRP_DLL_NOTIFICATION_BLOCK block = CONTAINING_RECORD(entry, DBG_LDRP_DLL_NOTIFICATION_BLOCK, Links);

		ptrdiff_t hookContextIndex = AddHookContext();
		if (hookContextIndex >= 0)
		{
			PDLL_NOTIFICATION_HOOK_CONTEXT hookContext = &g_hookContextArray.pItems[hookContextIndex];
			hookContext->OriginalNotificationFunction = block->NotificationFunction;
			hookContext->OriginalContext = block->Context;
			block->NotificationFunction = HookWrapperNotificationFunction;
			block->Context = (PVOID)hookContextIndex;
		}
	}
}

static VOID InsertHeadList(PLIST_ENTRY pHead, PLIST_ENTRY pEntry)
{
	pEntry->Blink = pHead;
	pEntry->Flink = pHead->Flink;

	pHead->Flink->Blink = pEntry;
	pHead->Flink = pEntry;
}

static VOID RemoveEntryList(PLIST_ENTRY pEntry)
{
	pEntry->Blink->Flink = pEntry->Flink;
	pEntry->Flink->Blink = pEntry->Blink;
}

BOOL HookLdrDllNotifications(PLDR_DLL_NOTIFICATION_FUNCTION_HOOK hook)
{
	HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
	if (!hNtdll)
	{
		return FALSE;
	}

	PLDR_REGISTER_DLL_NOTIFICATION pLdrRegisterDllNotification =
		(PLDR_REGISTER_DLL_NOTIFICATION)GetProcAddress(hNtdll, "LdrRegisterDllNotification");
	if (!pLdrRegisterDllNotification)
	{
		return FALSE;
	}

	PLDR_UNREGISTER_DLL_NOTIFICATION pLdrUnregisterDllNotification =
		(PLDR_UNREGISTER_DLL_NOTIFICATION)GetProcAddress(hNtdll, "LdrUnregisterDllNotification");
	if (!pLdrUnregisterDllNotification)
	{
		return FALSE;
	}

	PVOID cookie;
	NTSTATUS status = pLdrRegisterDllNotification(0, EmptyNotificationFunction, NULL, &cookie);
	if (!NT_SUCCESS(status))
	{
		return FALSE;
	}

	// Note: The operations below aren't thread-safe and are prone to races.

	PLIST_ENTRY head = ((PDBG_LDRP_DLL_NOTIFICATION_BLOCK)cookie)->Links.Flink;

	pLdrUnregisterDllNotification(cookie);

	g_hookDllNotification = hook;

	g_hookDllNotificationBlock.NotificationFunction = FirstNotificationFunction;

	InsertHeadList(head, &g_hookDllNotificationBlock.Links);

	return TRUE;
}

void UnhookLdrDllNotifications()
{
	PLIST_ENTRY head = g_hookDllNotificationBlock.Links.Blink;

	// Note: The operations below aren't thread-safe and are prone to races.

	RemoveEntryList(&g_hookDllNotificationBlock.Links);

	for (PLIST_ENTRY entry = head->Flink; entry != head; entry = entry->Flink)
	{
		PDBG_LDRP_DLL_NOTIFICATION_BLOCK block = CONTAINING_RECORD(entry, DBG_LDRP_DLL_NOTIFICATION_BLOCK, Links);

		if (block->NotificationFunction == HookWrapperNotificationFunction)
		{
			ptrdiff_t hookContextIndex = (ptrdiff_t)block->Context;
			PDLL_NOTIFICATION_HOOK_CONTEXT hookContext = &g_hookContextArray.pItems[hookContextIndex];
			block->NotificationFunction = hookContext->OriginalNotificationFunction;
			block->Context = hookContext->OriginalContext;
		}
	}

	ClearHookContextArray();
}
