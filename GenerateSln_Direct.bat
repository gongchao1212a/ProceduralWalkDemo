@echo off

:: ================= 配置区域 =================
:: 1. 修改为你的 UnrealBuildTool.exe 路径
set "UBT_PATH=C:\Depot\Engine\ue4_rg\Engine\Binaries\DotNET\UnrealBuildTool.exe"



:: 执行 UBT 生成命令
"%UBT_PATH%" -projectfiles -project="F:\ProceduralWalkDemo\ProceduralWalkDemo.uproject" -game -engine -progress -2019

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [成功] .sln 文件已生成。
) else (
    echo.
    echo [失败] 生成过程中出现错误，请检查路径是否正确。
)

pause