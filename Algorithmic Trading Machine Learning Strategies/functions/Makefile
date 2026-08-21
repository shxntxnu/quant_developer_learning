CXX = g++
CXXFLAGS = -Wall -O2 -Iinclude

BIN_DIR = bin
SRC_DIR = src
INC_DIR = include

DLLS = $(BIN_DIR)/MathLib.dll \
       $(BIN_DIR)/calculateYTMYield.dll \
       $(BIN_DIR)/calculateZeroCouponYield.dll \
       $(BIN_DIR)/calculateADF.dll \
       $(BIN_DIR)/calculateCointegration.dll

all: $(BIN_DIR) $(DLLS) $(BIN_DIR)/main.exe

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/MathLib.dll: $(SRC_DIR)/MathLib.cpp $(INC_DIR)/MathLib.h
	$(CXX) $(CXXFLAGS) -DMATHLIB_EXPORTS -shared -o $@ $< -Wl,--out-implib,$(BIN_DIR)/libMathLib.a

$(BIN_DIR)/calculateYTMYield.dll: $(SRC_DIR)/calculateYTMYield.cpp $(INC_DIR)/calculateYTMYield.h
	$(CXX) $(CXXFLAGS) -DCALCULATEYTMYIELD_EXPORTS -shared -o $@ $< -Wl,--out-implib,$(BIN_DIR)/libcalculateYTMYield.a

$(BIN_DIR)/calculateZeroCouponYield.dll: $(SRC_DIR)/calculateZeroCouponYield.cpp $(INC_DIR)/calculateZeroCouponYield.h
	$(CXX) $(CXXFLAGS) -DCALCULATEZEROCOUPONYIELD_EXPORTS -shared -o $@ $< -Wl,--out-implib,$(BIN_DIR)/libcalculateZeroCouponYield.a

$(BIN_DIR)/calculateADF.dll: $(SRC_DIR)/calculateADF.cpp $(INC_DIR)/calculateADF.h
	$(CXX) $(CXXFLAGS) -DCALCULATEADF_EXPORTS -shared -o $@ $< -Wl,--out-implib,$(BIN_DIR)/libcalculateADF.a

$(BIN_DIR)/calculateCointegration.dll: $(SRC_DIR)/calculateCointegration.cpp $(INC_DIR)/calculateCointegration.h
	$(CXX) $(CXXFLAGS) -DCALCULATECOINTEGRATION_EXPORTS -shared -o $@ $< -Wl,--out-implib,$(BIN_DIR)/libcalculateCointegration.a

$(BIN_DIR)/main.exe: main.cpp $(DLLS)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp -L$(BIN_DIR) -lMathLib -lcalculateYTMYield -lcalculateZeroCouponYield -lcalculateADF -lcalculateCointegration

run: $(BIN_DIR)/main.exe
	./$(BIN_DIR)/main.exe

clean:
	del /Q bin\*.dll bin\*.a bin\*.exe 2>nul || rm -rf bin/*.dll bin/*.a bin/*.exe
