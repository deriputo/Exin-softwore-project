param([string]$Toolchain = 'E:\keil5\ARM\ARMCC\bin')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOutput = Join-Path $taskRoot 'build/keil'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
[xml]$project = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'eXin.uvprojx') -Raw
$target = $project.Project.Targets.Target
$compileFlags = @('--cpu=Cortex-M3','--c99','--apcs=interwork','--library_type=microlib',
                  '--split_sections','--no-multibyte-chars','-O2','-g',
                  '-DSTM32F10X_MD','-DUSE_STDPERIPH_DRIVER','-DHSE_VALUE=8000000')
foreach ($include in $target.TargetOption.TargetArmAds.Cads.VariousControls.IncludePath.Split(';')) {
    # Paths remain relative, avoiding compiler command-line codepage problems.
    $compileFlags += '-I' + ('Project/' + $include).Replace('\','/')
}
$objects = @()
Push-Location $taskRoot
try {
    foreach ($file in $target.Groups.Group.Files.File) {
        $source = ('Project/' + $file.FilePath).Replace('\','/')
        if ($file.FileType -eq '5') { continue }
        $objName = ($file.FilePath -replace '[\\/ .]', '_') + '.o'
        $objPath = 'build/keil/' + $objName
        if ($file.FileType -eq '2') {
            & (Join-Path $Toolchain 'armasm.exe') '--cpu=Cortex-M3' '--apcs=interwork' '--pd' '__MICROLIB SETA 1' '-g' $source '-o' $objPath
        } else {
            & (Join-Path $Toolchain 'armcc.exe') @compileFlags '-c' $source '-o' $objPath
        }
        if ($LASTEXITCODE -ne 0) { throw "ARMCC build failed: $source" }
        $objects += $objPath
    }
    & (Join-Path $Toolchain 'armlink.exe') '--cpu=Cortex-M3' '--library_type=microlib' @objects `
        '--scatter=Project/eXin.sct' '--map' '--symbols' '--info=sizes,totals,unused,veneers' `
        '--list=build/keil/eXin.map' '--output=build/keil/eXin.axf'
    if ($LASTEXITCODE -ne 0) { throw 'ARMCC link failed.' }
    & (Join-Path $Toolchain 'fromelf.exe') '--i32combined' '--output=build/keil/eXin.hex' 'build/keil/eXin.axf'
    if ($LASTEXITCODE -ne 0) { throw 'ARMCC HEX generation failed.' }
    & (Join-Path $Toolchain 'fromelf.exe') '--bin' '--output=build/keil/eXin.bin' 'build/keil/eXin.axf'
    if ($LASTEXITCODE -ne 0) { throw 'ARMCC BIN generation failed.' }
    Write-Output 'Keil ARMCC5 build complete: build/keil/eXin.hex'
} finally { Pop-Location }
