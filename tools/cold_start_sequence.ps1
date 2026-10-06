$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$statePath = Join-Path $root 'build/cold_start_state.json'
$resultPath = Join-Path $root 'build/cold_start_results.csv'
$exe = Join-Path $root 'build/mvo.exe'
$data = Join-Path $root 'data.xls'

if (Test-Path $statePath) { $state = Get-Content $statePath -Raw | ConvertFrom-Json } else { $state = [pscustomobject]@{ NextMode = 'high' } }
$mode = [string]$state.NextMode

$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $exe
$info.WorkingDirectory = $root
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.Arguments = '"' + $data + '"' + $(if ($mode -eq 'compat') { ' --compat' } else { ' --high' })
$p = [Diagnostics.Process]::new()
$p.StartInfo = $info
$watch = [Diagnostics.Stopwatch]::StartNew()
$null = $p.Start()
$outTask = $p.StandardOutput.ReadToEndAsync()
$errTask = $p.StandardError.ReadToEndAsync()
$p.WaitForExit()
$watch.Stop()
$stdout = $outTask.GetAwaiter().GetResult()
$stderr = $errTask.GetAwaiter().GetResult()
$exitCode = $p.ExitCode
$p.Dispose()

$row = [pscustomobject]@{ Timestamp = (Get-Date).ToUniversalTime().ToString('o'); Mode = $mode; WallMs = [math]::Round($watch.Elapsed.TotalMilliseconds, 3); ExitCode = $exitCode; Error = $stderr.Trim(); Sha256 = (Get-FileHash $exe -Algorithm SHA256).Hash }
if (Test-Path $resultPath) { $row | Export-Csv $resultPath -Append -NoTypeInformation -Encoding utf8 } else { $row | Export-Csv $resultPath -NoTypeInformation -Encoding utf8 }

if ($exitCode -ne 0) { throw "Cold-start process failed ($exitCode): $stderr" }
if ($mode -eq 'high') {
    [pscustomobject]@{ NextMode = 'compat' } | ConvertTo-Json | Set-Content $statePath -Encoding utf8
    shutdown.exe /r /t 15 /c "Cold-start benchmark: switching to compat mode" /d p:4:1
} else {
    [pscustomobject]@{ NextMode = 'done' } | ConvertTo-Json | Set-Content $statePath -Encoding utf8
    schtasks.exe /Change /TN "Codex Cold Start Benchmark" /Disable | Out-Null
}
