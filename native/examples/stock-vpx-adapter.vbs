' For either the native plugin or C# controller, add this helper and replace
' BOTH wrapper lines `GetMessages = m_bcpController.GetMessages` with:
'     GetMessages = BcpDrainMessages(m_bcpController)
' All GLF callers, message properties and protocol handling stay the same.
' Also supports the C# controller, which has GetMessages but no ReadMessage.
Function BcpDrainMessages(controller)
    Dim result(), count, message
    Dim errorNumber, errorSource, errorDescription

    ' Probe by reading once; retain that first message if the native API exists.
    ' Only a missing member permits fallback. Other failures must reach the caller.
    On Error Resume Next
    Err.Clear
    Set message = controller.ReadMessage()
    errorNumber = Err.Number
    errorSource = Err.Source
    errorDescription = Err.Description
    On Error GoTo 0

    Select Case errorNumber
        Case 438, -2147352570, -2147352573 ' Unsupported member / unknown name / member not found
            BcpDrainMessages = controller.GetMessages()
            Exit Function
        Case 0
            ' Native controller: continue draining below.
        Case Else
            Err.Raise errorNumber, errorSource, errorDescription
    End Select

    count = 0
    Do
        If message Is Nothing Then Exit Do
        ReDim Preserve result(count)
        Set result(count) = message
        count = count + 1
        Set message = controller.ReadMessage()
    Loop
    If count = 0 Then
        BcpDrainMessages = Array()
    Else
        BcpDrainMessages = result
    End If
End Function
