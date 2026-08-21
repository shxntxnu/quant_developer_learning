CXX = g++
CXXFLAGS = -Wall -O2

all: MathLib.dll calculateYTMYield.dll calculateZeroCouponYield.dll main.exe

MathLib.dll: MathLib.cpp MathLib.h
	$(CXX) $(CXXFLAGS) -DMATHLIB_EXPORTS -shared -o $@ MathLib.cpp -Wl,--out-implib,libMathLib.a

calculateYTMYield.dll: calculateYTMYield.cpp calculateYTMYield.h
	$(CXX) $(CXXFLAGS) -DCALCULATEYTMYIELD_EXPORTS -shared -o $@ calculateYTMYield.cpp -Wl,--out-implib,libcalculateYTMYield.a

calculateZeroCouponYield.dll: calculateZeroCouponYield.cpp calculateZeroCouponYield.h
	$(CXX) $(CXXFLAGS) -DCALCULATEZEROCOUPONYIELD_EXPORTS -shared -o $@ calculateZeroCouponYield.cpp -Wl,--out-implib,libcalculateZeroCouponYield.a

main.exe: main.cpp MathLib.dll calculateYTMYield.dll calculateZeroCouponYield.dll
	$(CXX) $(CXXFLAGS) -o $@ main.cpp -L. -lMathLib -lcalculateYTMYield -lcalculateZeroCouponYield

run: main.exe
	./main.exe

clean:
	del /Q *.dll *.a *.exe *.o 2>nul || true
