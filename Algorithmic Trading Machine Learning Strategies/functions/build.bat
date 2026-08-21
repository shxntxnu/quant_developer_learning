@echo off
echo =======================================================
echo          Building Quantitative Finance DLLs
echo =======================================================

if not exist "bin" mkdir bin

echo [1/6] Compiling MathLib.dll...
g++ -Iinclude -DMATHLIB_EXPORTS -shared -o bin/MathLib.dll src/MathLib.cpp -Wl,--out-implib,bin/libMathLib.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling MathLib.dll!
    exit /b %ERRORLEVEL%
)

echo [2/6] Compiling calculateYTMYield.dll...
g++ -Iinclude -DCALCULATEYTMYIELD_EXPORTS -shared -o bin/calculateYTMYield.dll src/calculateYTMYield.cpp -Wl,--out-implib,bin/libcalculateYTMYield.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling calculateYTMYield.dll!
    exit /b %ERRORLEVEL%
)

echo [3/6] Compiling calculateZeroCouponYield.dll...
g++ -Iinclude -DCALCULATEZEROCOUPONYIELD_EXPORTS -shared -o bin/calculateZeroCouponYield.dll src/calculateZeroCouponYield.cpp -Wl,--out-implib,bin/libcalculateZeroCouponYield.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling calculateZeroCouponYield.dll!
    exit /b %ERRORLEVEL%
)

echo [4/6] Compiling calculateADF.dll...
g++ -Iinclude -DCALCULATEADF_EXPORTS -shared -o bin/calculateADF.dll src/calculateADF.cpp -Wl,--out-implib,bin/libcalculateADF.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling calculateADF.dll!
    exit /b %ERRORLEVEL%
)

echo [5/6] Compiling calculateCointegration.dll...
g++ -Iinclude -DCALCULATECOINTEGRATION_EXPORTS -shared -o bin/calculateCointegration.dll src/calculateCointegration.cpp -Wl,--out-implib,bin/libcalculateCointegration.a
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling calculateCointegration.dll!
    exit /b %ERRORLEVEL%
)

echo [6/6] Compiling and Linking main.exe...
g++ -Iinclude -o bin/main.exe main.cpp -Lbin -lMathLib -lcalculateYTMYield -lcalculateZeroCouponYield -lcalculateADF -lcalculateCointegration
if %ERRORLEVEL% NEQ 0 (
    echo Error linking main.exe!
    exit /b %ERRORLEVEL%
)

echo.
echo =======================================================
echo               Build Successful! Running App...
echo =======================================================
echo.
bin\main.exe
