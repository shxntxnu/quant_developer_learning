@echo off
echo =======================================================
echo          Building Quantitative Finance DLLs
echo =======================================================

echo [1/4] Compiling MathLib.dll...
g++ -DMATHLIB_EXPORTS -shared -o MathLib.dll MathLib.cpp -Wl,--out-implib,libMathLib.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling MathLib.dll!
    exit /b %ERRORLEVEL%
)

echo [2/4] Compiling calculateYTMYield.dll...
g++ -DCALCULATEYTMYIELD_EXPORTS -shared -o calculateYTMYield.dll calculateYTMYield.cpp -Wl,--out-implib,libcalculateYTMYield.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling calculateYTMYield.dll!
    exit /b %ERRORLEVEL%
)

echo [3/4] Compiling calculateZeroCouponYield.dll...
g++ -DCALCULATEZEROCOUPONYIELD_EXPORTS -shared -o calculateZeroCouponYield.dll calculateZeroCouponYield.cpp -Wl,--out-implib,libcalculateZeroCouponYield.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling calculateZeroCouponYield.dll!
    exit /b %ERRORLEVEL%
)

echo [4/4] Compiling and Linking main.exe...
g++ -o main.exe main.cpp -L. -lMathLib -lcalculateYTMYield -lcalculateZeroCouponYield
if %ERRORLEVEL% NEQ 0 (
    echo Error linking main.exe!
    exit /b %ERRORLEVEL%
)

echo.
echo =======================================================
echo               Build Successful! Running App...
echo =======================================================
echo.
main.exe
