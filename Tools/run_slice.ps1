# Launch the slice detached from the calling shell so nothing reaps it.
param(
    [string]$Extra = "-ws2shot=-1",
    [string]$Log = "Build/slice_detached.log"
)

$project = "C:/Users/HP/.config/freebuff-desktop/local-projects/8bca0768-4b09-46b6-a759-12c2f59f0cf8"
$exe = "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"

$arguments = @(
    "$project/WiesbadenSurvivors2.uproject",
    "-game",
    "-windowed",
    "-ResX=1600",
    "-ResY=900",
    "-nosplash",
    "-abslog=$project/$Log"
) + ($Extra -split " ")

$process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
Write-Output "PID=$($process.Id)"
