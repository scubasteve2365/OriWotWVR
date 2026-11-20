; Generated: one jump per winhttp export, to the real System32 winhttp.dll
.data
PUBLIC g_real
g_real QWORD 92 DUP(0)
.code
Thunk_DllCanUnloadNow PROC
    jmp QWORD PTR [g_real + 0]
Thunk_DllCanUnloadNow ENDP
Thunk_DllGetClassObject PROC
    jmp QWORD PTR [g_real + 8]
Thunk_DllGetClassObject ENDP
Thunk_Private1 PROC
    jmp QWORD PTR [g_real + 16]
Thunk_Private1 ENDP
Thunk_SvchostPushServiceGlobals PROC
    jmp QWORD PTR [g_real + 24]
Thunk_SvchostPushServiceGlobals ENDP
Thunk_WinHttpAddRequestHeaders PROC
    jmp QWORD PTR [g_real + 32]
Thunk_WinHttpAddRequestHeaders ENDP
Thunk_WinHttpAddRequestHeadersEx PROC
    jmp QWORD PTR [g_real + 40]
Thunk_WinHttpAddRequestHeadersEx ENDP
Thunk_WinHttpAutoProxySvcMain PROC
    jmp QWORD PTR [g_real + 48]
Thunk_WinHttpAutoProxySvcMain ENDP
Thunk_WinHttpCheckPlatform PROC
    jmp QWORD PTR [g_real + 56]
Thunk_WinHttpCheckPlatform ENDP
Thunk_WinHttpCloseHandle PROC
    jmp QWORD PTR [g_real + 64]
Thunk_WinHttpCloseHandle ENDP
Thunk_WinHttpConnect PROC
    jmp QWORD PTR [g_real + 72]
Thunk_WinHttpConnect ENDP
Thunk_WinHttpConnectionDeletePolicyEntries PROC
    jmp QWORD PTR [g_real + 80]
Thunk_WinHttpConnectionDeletePolicyEntries ENDP
Thunk_WinHttpConnectionDeletePolicyEntriesByAppSid PROC
    jmp QWORD PTR [g_real + 88]
Thunk_WinHttpConnectionDeletePolicyEntriesByAppSid ENDP
Thunk_WinHttpConnectionDeleteProxyInfo PROC
    jmp QWORD PTR [g_real + 96]
Thunk_WinHttpConnectionDeleteProxyInfo ENDP
Thunk_WinHttpConnectionFreeNameList PROC
    jmp QWORD PTR [g_real + 104]
Thunk_WinHttpConnectionFreeNameList ENDP
Thunk_WinHttpConnectionFreeProxyInfo PROC
    jmp QWORD PTR [g_real + 112]
Thunk_WinHttpConnectionFreeProxyInfo ENDP
Thunk_WinHttpConnectionFreeProxyList PROC
    jmp QWORD PTR [g_real + 120]
Thunk_WinHttpConnectionFreeProxyList ENDP
Thunk_WinHttpConnectionGetNameList PROC
    jmp QWORD PTR [g_real + 128]
Thunk_WinHttpConnectionGetNameList ENDP
Thunk_WinHttpConnectionGetProxyInfo PROC
    jmp QWORD PTR [g_real + 136]
Thunk_WinHttpConnectionGetProxyInfo ENDP
Thunk_WinHttpConnectionGetProxyList PROC
    jmp QWORD PTR [g_real + 144]
Thunk_WinHttpConnectionGetProxyList ENDP
Thunk_WinHttpConnectionOnlyConvert PROC
    jmp QWORD PTR [g_real + 152]
Thunk_WinHttpConnectionOnlyConvert ENDP
Thunk_WinHttpConnectionOnlyReceive PROC
    jmp QWORD PTR [g_real + 160]
Thunk_WinHttpConnectionOnlyReceive ENDP
Thunk_WinHttpConnectionOnlySend PROC
    jmp QWORD PTR [g_real + 168]
Thunk_WinHttpConnectionOnlySend ENDP
Thunk_WinHttpConnectionSetPolicyEntries PROC
    jmp QWORD PTR [g_real + 176]
Thunk_WinHttpConnectionSetPolicyEntries ENDP
Thunk_WinHttpConnectionSetProxyInfo PROC
    jmp QWORD PTR [g_real + 184]
Thunk_WinHttpConnectionSetProxyInfo ENDP
Thunk_WinHttpConnectionUpdateIfIndexTable PROC
    jmp QWORD PTR [g_real + 192]
Thunk_WinHttpConnectionUpdateIfIndexTable ENDP
Thunk_WinHttpCrackUrl PROC
    jmp QWORD PTR [g_real + 200]
Thunk_WinHttpCrackUrl ENDP
Thunk_WinHttpCreateProxyList PROC
    jmp QWORD PTR [g_real + 208]
Thunk_WinHttpCreateProxyList ENDP
Thunk_WinHttpCreateProxyManager PROC
    jmp QWORD PTR [g_real + 216]
Thunk_WinHttpCreateProxyManager ENDP
Thunk_WinHttpCreateProxyResolver PROC
    jmp QWORD PTR [g_real + 224]
Thunk_WinHttpCreateProxyResolver ENDP
Thunk_WinHttpCreateProxyResult PROC
    jmp QWORD PTR [g_real + 232]
Thunk_WinHttpCreateProxyResult ENDP
Thunk_WinHttpCreateUiCompatibleProxyString PROC
    jmp QWORD PTR [g_real + 240]
Thunk_WinHttpCreateUiCompatibleProxyString ENDP
Thunk_WinHttpCreateUrl PROC
    jmp QWORD PTR [g_real + 248]
Thunk_WinHttpCreateUrl ENDP
Thunk_WinHttpDetectAutoProxyConfigUrl PROC
    jmp QWORD PTR [g_real + 256]
Thunk_WinHttpDetectAutoProxyConfigUrl ENDP
Thunk_WinHttpFreeProxyResult PROC
    jmp QWORD PTR [g_real + 264]
Thunk_WinHttpFreeProxyResult ENDP
Thunk_WinHttpFreeProxyResultEx PROC
    jmp QWORD PTR [g_real + 272]
Thunk_WinHttpFreeProxyResultEx ENDP
Thunk_WinHttpFreeProxySettings PROC
    jmp QWORD PTR [g_real + 280]
Thunk_WinHttpFreeProxySettings ENDP
Thunk_WinHttpFreeProxySettingsEx PROC
    jmp QWORD PTR [g_real + 288]
Thunk_WinHttpFreeProxySettingsEx ENDP
Thunk_WinHttpFreeQueryConnectionGroupResult PROC
    jmp QWORD PTR [g_real + 296]
Thunk_WinHttpFreeQueryConnectionGroupResult ENDP
Thunk_WinHttpGetDefaultProxyConfiguration PROC
    jmp QWORD PTR [g_real + 304]
Thunk_WinHttpGetDefaultProxyConfiguration ENDP
Thunk_WinHttpGetIEProxyConfigForCurrentUser PROC
    jmp QWORD PTR [g_real + 312]
Thunk_WinHttpGetIEProxyConfigForCurrentUser ENDP
Thunk_WinHttpGetProxyForUrl PROC
    jmp QWORD PTR [g_real + 320]
Thunk_WinHttpGetProxyForUrl ENDP
Thunk_WinHttpGetProxyForUrlEx PROC
    jmp QWORD PTR [g_real + 328]
Thunk_WinHttpGetProxyForUrlEx ENDP
Thunk_WinHttpGetProxyForUrlEx2 PROC
    jmp QWORD PTR [g_real + 336]
Thunk_WinHttpGetProxyForUrlEx2 ENDP
Thunk_WinHttpGetProxyForUrlHvsi PROC
    jmp QWORD PTR [g_real + 344]
Thunk_WinHttpGetProxyForUrlHvsi ENDP
Thunk_WinHttpGetProxyResult PROC
    jmp QWORD PTR [g_real + 352]
Thunk_WinHttpGetProxyResult ENDP
Thunk_WinHttpGetProxyResultEx PROC
    jmp QWORD PTR [g_real + 360]
Thunk_WinHttpGetProxyResultEx ENDP
Thunk_WinHttpGetProxySettingsEx PROC
    jmp QWORD PTR [g_real + 368]
Thunk_WinHttpGetProxySettingsEx ENDP
Thunk_WinHttpGetProxySettingsResultEx PROC
    jmp QWORD PTR [g_real + 376]
Thunk_WinHttpGetProxySettingsResultEx ENDP
Thunk_WinHttpGetProxySettingsVersion PROC
    jmp QWORD PTR [g_real + 384]
Thunk_WinHttpGetProxySettingsVersion ENDP
Thunk_WinHttpGetTunnelSocket PROC
    jmp QWORD PTR [g_real + 392]
Thunk_WinHttpGetTunnelSocket ENDP
Thunk_WinHttpOpen PROC
    jmp QWORD PTR [g_real + 400]
Thunk_WinHttpOpen ENDP
Thunk_WinHttpOpenRequest PROC
    jmp QWORD PTR [g_real + 408]
Thunk_WinHttpOpenRequest ENDP
Thunk_WinHttpPacJsWorkerMain PROC
    jmp QWORD PTR [g_real + 416]
Thunk_WinHttpPacJsWorkerMain ENDP
Thunk_WinHttpProbeConnectivity PROC
    jmp QWORD PTR [g_real + 424]
Thunk_WinHttpProbeConnectivity ENDP
Thunk_WinHttpProtocolCompleteUpgrade PROC
    jmp QWORD PTR [g_real + 432]
Thunk_WinHttpProtocolCompleteUpgrade ENDP
Thunk_WinHttpProtocolReceive PROC
    jmp QWORD PTR [g_real + 440]
Thunk_WinHttpProtocolReceive ENDP
Thunk_WinHttpProtocolSend PROC
    jmp QWORD PTR [g_real + 448]
Thunk_WinHttpProtocolSend ENDP
Thunk_WinHttpQueryAuthSchemes PROC
    jmp QWORD PTR [g_real + 456]
Thunk_WinHttpQueryAuthSchemes ENDP
Thunk_WinHttpQueryConnectionGroup PROC
    jmp QWORD PTR [g_real + 464]
Thunk_WinHttpQueryConnectionGroup ENDP
Thunk_WinHttpQueryDataAvailable PROC
    jmp QWORD PTR [g_real + 472]
Thunk_WinHttpQueryDataAvailable ENDP
Thunk_WinHttpQueryHeaders PROC
    jmp QWORD PTR [g_real + 480]
Thunk_WinHttpQueryHeaders ENDP
Thunk_WinHttpQueryHeadersEx PROC
    jmp QWORD PTR [g_real + 488]
Thunk_WinHttpQueryHeadersEx ENDP
Thunk_WinHttpQueryOption PROC
    jmp QWORD PTR [g_real + 496]
Thunk_WinHttpQueryOption ENDP
Thunk_WinHttpReadData PROC
    jmp QWORD PTR [g_real + 504]
Thunk_WinHttpReadData ENDP
Thunk_WinHttpReadDataEx PROC
    jmp QWORD PTR [g_real + 512]
Thunk_WinHttpReadDataEx ENDP
Thunk_WinHttpReadProxySettings PROC
    jmp QWORD PTR [g_real + 520]
Thunk_WinHttpReadProxySettings ENDP
Thunk_WinHttpReadProxySettingsHvsi PROC
    jmp QWORD PTR [g_real + 528]
Thunk_WinHttpReadProxySettingsHvsi ENDP
Thunk_WinHttpReceiveResponse PROC
    jmp QWORD PTR [g_real + 536]
Thunk_WinHttpReceiveResponse ENDP
Thunk_WinHttpRefreshProxySettings PROC
    jmp QWORD PTR [g_real + 544]
Thunk_WinHttpRefreshProxySettings ENDP
Thunk_WinHttpRegisterProxyChangeNotification PROC
    jmp QWORD PTR [g_real + 552]
Thunk_WinHttpRegisterProxyChangeNotification ENDP
Thunk_WinHttpResetAutoProxy PROC
    jmp QWORD PTR [g_real + 560]
Thunk_WinHttpResetAutoProxy ENDP
Thunk_WinHttpResolverGetProxyForUrl PROC
    jmp QWORD PTR [g_real + 568]
Thunk_WinHttpResolverGetProxyForUrl ENDP
Thunk_WinHttpSaveProxyCredentials PROC
    jmp QWORD PTR [g_real + 576]
Thunk_WinHttpSaveProxyCredentials ENDP
Thunk_WinHttpSendRequest PROC
    jmp QWORD PTR [g_real + 584]
Thunk_WinHttpSendRequest ENDP
Thunk_WinHttpSetCredentials PROC
    jmp QWORD PTR [g_real + 592]
Thunk_WinHttpSetCredentials ENDP
Thunk_WinHttpSetDefaultProxyConfiguration PROC
    jmp QWORD PTR [g_real + 600]
Thunk_WinHttpSetDefaultProxyConfiguration ENDP
Thunk_WinHttpSetOption PROC
    jmp QWORD PTR [g_real + 608]
Thunk_WinHttpSetOption ENDP
Thunk_WinHttpSetProxySettingsPerUser PROC
    jmp QWORD PTR [g_real + 616]
Thunk_WinHttpSetProxySettingsPerUser ENDP
Thunk_WinHttpSetSecureLegacyServersAppCompat PROC
    jmp QWORD PTR [g_real + 624]
Thunk_WinHttpSetSecureLegacyServersAppCompat ENDP
Thunk_WinHttpSetStatusCallback PROC
    jmp QWORD PTR [g_real + 632]
Thunk_WinHttpSetStatusCallback ENDP
Thunk_WinHttpSetTimeouts PROC
    jmp QWORD PTR [g_real + 640]
Thunk_WinHttpSetTimeouts ENDP
Thunk_WinHttpTimeFromSystemTime PROC
    jmp QWORD PTR [g_real + 648]
Thunk_WinHttpTimeFromSystemTime ENDP
Thunk_WinHttpTimeToSystemTime PROC
    jmp QWORD PTR [g_real + 656]
Thunk_WinHttpTimeToSystemTime ENDP
Thunk_WinHttpUnregisterProxyChangeNotification PROC
    jmp QWORD PTR [g_real + 664]
Thunk_WinHttpUnregisterProxyChangeNotification ENDP
Thunk_WinHttpWebSocketClose PROC
    jmp QWORD PTR [g_real + 672]
Thunk_WinHttpWebSocketClose ENDP
Thunk_WinHttpWebSocketCompleteUpgrade PROC
    jmp QWORD PTR [g_real + 680]
Thunk_WinHttpWebSocketCompleteUpgrade ENDP
Thunk_WinHttpWebSocketQueryCloseStatus PROC
    jmp QWORD PTR [g_real + 688]
Thunk_WinHttpWebSocketQueryCloseStatus ENDP
Thunk_WinHttpWebSocketReceive PROC
    jmp QWORD PTR [g_real + 696]
Thunk_WinHttpWebSocketReceive ENDP
Thunk_WinHttpWebSocketSend PROC
    jmp QWORD PTR [g_real + 704]
Thunk_WinHttpWebSocketSend ENDP
Thunk_WinHttpWebSocketShutdown PROC
    jmp QWORD PTR [g_real + 712]
Thunk_WinHttpWebSocketShutdown ENDP
Thunk_WinHttpWriteData PROC
    jmp QWORD PTR [g_real + 720]
Thunk_WinHttpWriteData ENDP
Thunk_WinHttpWriteProxySettings PROC
    jmp QWORD PTR [g_real + 728]
Thunk_WinHttpWriteProxySettings ENDP
END
