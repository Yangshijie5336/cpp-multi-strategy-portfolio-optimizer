param([int]$Runs = 20)
$ErrorActionPreference = 'Stop'
if ($Runs -lt 1) { throw 'Runs must be positive' }
$projectRoot = Split-Path $PSScriptRoot -Parent
$exePath = Join-Path $projectRoot 'build/mvo.exe'
$dataPath = Join-Path $projectRoot 'data.xls'
$results = @()
foreach ($mode in @('high', 'compat')) {
    for ($run = 0; $run -le $Runs; $run++) {
        $info = [System.Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $exePath
        $info.WorkingDirectory = $projectRoot
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        $info.Arguments = '"' + $dataPath + '"'
        if ($mode -eq 'compat') { $info.Arguments += ' --compat' }
        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $info
        # Includes process creation, loader/DLL startup, computation, output,
        # and exit. Parent-side result parsing happens after stopping the clock.
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $null = $process.Start()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $watch.Stop()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        $exitCode = $process.ExitCode
        $process.Dispose()
        if ($exitCode -ne 0) { throw "Process failed ($exitCode): $stderr" }
        $internal = [regex]::Match($stdout, 'time_total_ms=([0-9.]+)')
        $results += [pscustomobject]@{
            Mode = $mode; Run = $run; Phase = $(if ($run -eq 0) { 'first' } else { 'repeat' })
            WallMs = $watch.Elapsed.TotalMilliseconds
            InternalMs = $(if ($internal.Success) { [double]::Parse($internal.Groups[1].Value, [cultureinfo]::InvariantCulture) } else { $null })
            ExitCode = $exitCode
        }
    }
}
$outputDir = Join-Path $projectRoot 'build/process_benchmark'
$null = New-Item -ItemType Directory -Force -Path $outputDir
$results | Export-Csv (Join-Path $outputDir 'runs.csv') -NoTypeInformation -Encoding utf8
$summary = foreach ($mode in @('high', 'compat')) {
    $first = $results | Where-Object { $_.Mode -eq $mode -and $_.Phase -eq 'first' }
    $ordered = @($results | Where-Object { $_.Mode -eq $mode -and $_.Phase -eq 'repeat' } | Sort-Object WallMs)
    $median = if ($Runs % 2) { $ordered[[int][math]::Floor($Runs / 2)].WallMs } else {
        ($ordered[$Runs / 2 - 1].WallMs + $ordered[$Runs / 2].WallMs) / 2
    }
    [pscustomobject]@{
        Mode = $mode; RepeatedRuns = $Runs
        FirstMs = [math]::Round($first.WallMs, 3)
        MedianMs = [math]::Round($median, 3)
        P95Ms = [math]::Round($ordered[[int][math]::Ceiling(.95 * $Runs) - 1].WallMs, 3)
        MaxMs = [math]::Round($ordered[-1].WallMs, 3)
        MinMs = [math]::Round($ordered[0].WallMs, 3)
        InternalMeanMs = [math]::Round(($ordered | Measure-Object InternalMs -Average).Average, 3)
        AllUnder1s = (@($results | Where-Object { $_.Mode -eq $mode -and $_.WallMs -ge 1000 }).Count -eq 0)
    }
}
$summary | ConvertTo-Json | Set-Content (Join-Path $outputDir 'summary.json') -Encoding utf8
$summary | Format-Table -AutoSize | Out-String | Write-Output
Write-Output "Executable SHA256: $((Get-FileHash $exePath -Algorithm SHA256).Hash)"
Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | Format-Table -AutoSize
Write-Output "Raw results: $outputDir"
