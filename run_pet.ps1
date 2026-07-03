$exePath = "C:\Users\余敏涵\Desktop\DesktopPet\build\Release\DesktopPet.exe"
$workDir = "C:\Users\余敏涵\Desktop\DesktopPet\build\Release"

Write-Output "Launching: $exePath"
Write-Output "WorkingDir: $workDir"

try {
    $p = Start-Process -FilePath $exePath -WorkingDirectory $workDir -NoNewWindow -Wait -PassThru
    Write-Output "Exit code: $($p.ExitCode)"
} catch {
    Write-Error $_.Exception.Message
}
