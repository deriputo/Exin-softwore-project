param([string]$Compiler = 'C:\win64devkit\w64devkit\bin\gcc.exe', [string]$TestName = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:TMPDIR = $out
$includes = @('Application','Protocol')
$includes += @(Get-ChildItem -LiteralPath (Join-Path $root 'Drivers') -Directory | ForEach-Object { $_.FullName })
$font = @('Drivers/OLED/OLED_Data_ASCII.c','Drivers/OLED/OLED_Data_Hanzi.c')
$cases = @(
    @{name='key_core'; sources=@('Drivers/Key/key_core.c')},
    @{name='pwm_core'; sources=@('Drivers/PWM/pwm_core.c')},
    @{name='encoder_core'; sources=@('Drivers/Encoder/encoder_core.c')},
    @{name='rtc_core'; sources=@('Drivers/RTC/rtc_core.c')},
    @{name='rtc_backup'; sources=@('Drivers/RTC/rtc_core.c')},
    @{name='rtc_clock_state'; fake='tests/rtc_fake'; sources=@('Drivers/RTC/rtc_core.c','Drivers/RTC/rtc_clock.c')},
    @{name='power_core'; sources=@('Drivers/Power/power_core.c')},
    @{name='power_state'; fake='tests/power_fake'; sources=@('Drivers/Power/power_core.c','Drivers/Power/power.c')},
    @{name='serial_ring'; sources=@('Drivers/Serial/serial_ring.c')},
    @{name='control'; sources=@('Drivers/Control/control.c')},
    @{name='control_direct'; sources=@('Drivers/Control/control.c')},
    @{name='control_position'; sources=@('Drivers/Control/control.c')},
    @{name='control_rate'; sources=@('Drivers/Control/control.c')},
    @{name='protocol'; sources=@('Protocol/protocol.c')},
    @{name='ui_core'; sources=@('Application/ui_core.c')},
    @{name='imu_core'; sources=@('Drivers/IMU/imu_core.c')},
    @{name='imu_bridge_control'; sources=@('Drivers/IMU/imu_core.c','Drivers/Control/control.c')},
    @{name='oled_gfx'; sources=@('Drivers/OLED/oled_gfx.c') + $font},
    @{name='i2c_bus'; fake='tests/i2c_fake'; sources=@('Drivers/I2C/i2c_bus.c')},
    @{name='display_imu_state'; sources=@('Drivers/IMU/imu.c','Drivers/IMU/imu_core.c','Drivers/OLED/oled.c','Drivers/OLED/oled_gfx.c') + $font}
)
if (Test-Path (Join-Path $root 'tests/app_integration_test.c')) {
    $cases += @{name='app_integration'; fake='tests/app_fake'; sources=@('Application/app.c','Application/ui_core.c','Protocol/protocol.c','Drivers/Control/control.c','Drivers/Power/power_core.c','Drivers/RTC/rtc_core.c','Drivers/OLED/oled_gfx.c') + $font}
}
if ($TestName) {
    $cases = @($cases | Where-Object { $_.name -eq $TestName })
    if (!$cases.Count) { throw ('Unknown test case: ' + $TestName) }
}
Push-Location $root
try {
foreach ($case in $cases) {
    $flags = @('-std=c99','-O2','-Wall','-Wextra','-Werror','-pedantic')
    if ($case.fake) { $flags += '-I' + (Join-Path $root $case.fake) }
    foreach ($dir in $includes) {
        if (![System.IO.Path]::IsPathRooted($dir)) { $dir = Join-Path $root $dir }
        $flags += '-I' + $dir
    }
    $sources = @((Join-Path $root ('tests/' + $case.name + '_test.c')))
    foreach ($source in $case.sources) { $sources += Join-Path $root $source }
    # MinGW ld uses narrow Windows paths; keep its output path relative.
    $exe = 'build/tests/' + $case.name + '_test.exe'
    & $Compiler @flags @sources '-lm' '-o' $exe
    if ($LASTEXITCODE -ne 0) { throw ('Compile failed: ' + $case.name) }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw ('Test failed: ' + $case.name) }
}
Write-Output ('Passed ' + $cases.Count + ' C test suites.')
} finally { Pop-Location }
