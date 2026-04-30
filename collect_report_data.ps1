param(
    [int]$Log2Buffer = 20,
    [int]$DefaultLog2HashBins = 20
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Join-Path $Root "wordIndexer\wordIndexer.vcxproj"
$Exe = Join-Path $Root "wordIndexer\x64\Release\wordIndexer.exe"
$OutDir = Join-Path $Root "report_data"
$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

& $MSBuild $Project /p:Configuration=Release /p:Platform=x64 /t:Build /nologo /v:minimal

$FullThreads = [Environment]::ProcessorCount
if ($FullThreads -gt 64) { $FullThreads = 64 }

$SummaryAggregate = Join-Path $OutDir "summary_all_runs.csv"
$RunLog = Join-Path $OutDir "run_log.txt"

if (Test-Path $SummaryAggregate) { Remove-Item $SummaryAggregate }
if (Test-Path $RunLog) { Remove-Item $RunLog }

function Get-DatasetName([string]$Path) {
    return [IO.Path]::GetFileNameWithoutExtension($Path)
}

function Add-SummaryRow([string]$Category, [string]$Label, [string]$InputPath, [int]$Threads, [int]$Log2HashBins) {
    $dataset = Get-DatasetName $InputPath
    $summary = Join-Path $OutDir ("{0}_b{1}_t{2}_h{3}_summary.csv" -f $dataset, $Log2Buffer, $Threads, $Log2HashBins)
    if (!(Test-Path $summary)) {
        throw "Expected summary file was not created: $summary"
    }

    $row = Import-Csv $summary
    $row | Add-Member -NotePropertyName category -NotePropertyValue $Category
    $row | Add-Member -NotePropertyName label -NotePropertyValue $Label

    if (Test-Path $SummaryAggregate) {
        $row | Export-Csv $SummaryAggregate -Append -NoTypeInformation
    } else {
        $row | Export-Csv $SummaryAggregate -NoTypeInformation
    }
}

function Invoke-IndexerRun([string]$Category, [string]$Label, [string]$InputName, [int]$Threads, [int]$Log2HashBins) {
    $inputPath = Join-Path $Root $InputName
    if (!(Test-Path $inputPath)) {
        throw "Missing input file: $inputPath"
    }

    $header = "=== {0}: {1}, B=2^{2}, threads={3}, H=2^{4} ===" -f $Category, $InputName, $Log2Buffer, $Threads, $Log2HashBins
    $header | Tee-Object -FilePath $RunLog -Append
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & $Exe $Log2Buffer $inputPath $Threads $Log2HashBins 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    $output | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $RunLog -Append
    if ($exitCode -ne 0) {
        throw "Indexer failed with exit code $exitCode"
    }
    Add-SummaryRow $Category $Label $inputPath $Threads $Log2HashBins
}

$datasets = @(
    @{ Name = "enwiki-tiny.txt";   Label = "tiny" },
    @{ Name = "enwiki-small.txt";  Label = "small" },
    @{ Name = "enwiki-medium.txt"; Label = "medium" },
    @{ Name = "enwiki-all.txt";    Label = "all" }
)

foreach ($d in $datasets) {
    Invoke-IndexerRun "dataset" $d.Label $d.Name $FullThreads $DefaultLog2HashBins
}

$threadCandidates = @(1, 2, 4, 8, 12, 16, 24, 32, 64)
foreach ($t in $threadCandidates) {
    if ($t -le $FullThreads) {
        Invoke-IndexerRun "thread_sweep" ("threads_{0}" -f $t) "enwiki-medium.txt" $t $DefaultLog2HashBins
    }
}

foreach ($h in @(16, 18, 20, 22)) {
    Invoke-IndexerRun "hash_sweep" ("h_{0}" -f $h) "enwiki-medium.txt" $FullThreads $h
}

"Wrote aggregate summary: $SummaryAggregate" | Tee-Object -FilePath $RunLog -Append
