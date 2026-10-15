param([string]$Compiler = '', [string]$OutputDirectory = 'build')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$Compiler) {
    if ($env:ARM_GCC_BIN) { $Compiler = Join-Path $env:ARM_GCC_BIN 'arm-none-eabi-gcc.exe' }
    else {
        $found = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
        if ($found) { $Compiler = $found.Source }
        else {
            $Compiler = 'E:\stIDE\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin\arm-none-eabi-gcc.exe'
        }
    }
}
if (!(Test-Path -LiteralPath $Compiler)) { throw 'Set ARM_GCC_BIN or pass -Compiler with the ARM GCC executable path.' }
$bin = Split-Path -Parent $Compiler
$out = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force -Path $out | Out-Null
$taskTemp = Join-Path $out 'tmp'
New-Item -ItemType Directory -Force -Path $taskTemp | Out-Null
$env:TMPDIR = $taskTemp
$includeDirs = @('Application','Protocol','Project','Vendor/LocalSPL/Start','Vendor/LocalSPL/Library')
$includeDirs += @(Get-ChildItem -LiteralPath (Join-Path $root 'Drivers') -Directory | ForEach-Object { $_.FullName })
$flags = @('-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-Os','-g3','-std=c99',
           '-ffunction-sections','-fdata-sections','-Wall','-Wextra',
           '-DSTM32F10X_MD','-DUSE_STDPERIPH_DRIVER','-DHSE_VALUE=8000000')
foreach ($dir in $includeDirs) {
    if (![System.IO.Path]::IsPathRooted($dir)) { $dir = Join-Path $root $dir }
    $flags += '-I' + $dir
}
$sources = @(Get-ChildItem -Path (Join-Path $root 'Drivers') -Recurse -Filter '*.c' | ForEach-Object { $_.FullName })
$sources += @(Get-ChildItem -Path (Join-Path $root 'Application') -Filter '*.c' | ForEach-Object { $_.FullName })
$sources += Join-Path $root 'Protocol/protocol.c'
$sources += Join-Path $root 'Project/cmsis_gcc_compat.c'
$sources += Join-Path $root 'Project/syscalls.c'
$sources += Join-Path $root 'Vendor/LocalSPL/Start/system_stm32f10x.c'
foreach ($name in @('bkp','exti','gpio','i2c','pwr','rcc','rtc','tim','usart')) {
    $sources += Join-Path $root ('Vendor/LocalSPL/Library/stm32f10x_' + $name + '.c')
}
$sources += Join-Path $root 'Vendor/LocalSPL/Library/misc.c'
$sources += Join-Path $root 'Vendor/STM32F10x_StdPeriph_Lib/Libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x/startup/gcc_ride7/startup_stm32f10x_md.s'
$objects = @()
foreach ($source in $sources) {
    $relative = $source.Substring($root.Length + 1)
    $object = Join-Path $out (($relative -replace '[\\/ .]','_') + '.o')
    & $Compiler @flags '-c' $source '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $relative" }
    $objects += $object
}
$elf = Join-Path $out 'firmware.elf'
$linker = Join-Path $root 'Project/stm32f103c8.ld'
$map = Join-Path $out 'firmware.map'
& $Compiler '-mcpu=cortex-m3' '-mthumb' '-mfloat-abi=soft' '--specs=nano.specs' '--specs=nosys.specs' @objects '-T' $linker '-Wl,--gc-sections' "-Wl,-Map=$map" '-lm' '-o' $elf
if ($LASTEXITCODE -ne 0) { throw 'Link failed.' }
& (Join-Path $bin 'arm-none-eabi-objcopy.exe') '-O' 'ihex' $elf (Join-Path $out 'firmware.hex')
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed.' }
& (Join-Path $bin 'arm-none-eabi-objcopy.exe') '-O' 'binary' $elf (Join-Path $out 'firmware.bin')
if ($LASTEXITCODE -ne 0) { throw 'BIN generation failed.' }
& (Join-Path $bin 'arm-none-eabi-size.exe') $elf
Write-Output ('Firmware: ' + $elf)
