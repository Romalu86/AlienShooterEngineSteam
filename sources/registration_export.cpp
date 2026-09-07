namespace as1
{
    namespace
    {
        char g_registrationEmpty = '\0';
        char* g_registrationInformation = &g_registrationEmpty;
    }
}

__declspec(dllexport) void __stdcall GetRegistrationInformation(char* information)
{
    as1::g_registrationInformation = information;
}
