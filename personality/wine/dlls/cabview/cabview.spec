# The cabinet folder (CLSID_CABFolder) is registered for cabview.dll, as on
# Windows; zipfldr.dll implements it.
@ stdcall -private DllCanUnloadNow() zipfldr.DllCanUnloadNow
@ stdcall -private DllGetClassObject(ptr ptr ptr) zipfldr.DllGetClassObject
