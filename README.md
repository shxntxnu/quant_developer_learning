# Quant Developer Learning & Algorithmic Trading Strategies

Comprehensive repository for quantitative developer training, high-performance financial math DLLs in C++, and algorithmic trading strategies using machine learning.

---

## Table of Contents
1. [Project Overview](#project-overview)
2. [Component Architecture](#component-architecture)
   - [Current Implemented Components](#current-implemented-components)
   - [Planned / Upcoming Components](#planned--upcoming-components)
3. [User Guide & How to Run](#user-guide--how-to-run)
   - [1. Building & Running the C++ Quantitative DLLs](#1-building--running-the-c-quantitative-dlls)
   - [2. Running the Python Machine Learning Trading Strategy](#2-running-the-python-machine-learning-trading-strategy)
   - [3. How to Add a New C++ DLL Component](#3-how-to-add-a-new-c-dll-component)
4. [Directory Structure](#directory-structure)

---

## Project Overview

This repository bridges two critical domains for quantitative developers:
* **High-Performance C++ Core:** Modular, independent Windows Dynamic Link Libraries (DLLs) implementing low-latency pricing, yield root-finding, and financial calculations.
* **Python Research & Strategy Engine:** Unsupervised machine learning models (K-Means Clustering), Fama-French multi-factor risk models, technical feature engineering (`pandas_ta`), and portfolio optimization (`PyPortfolioOpt`).

---

## Component Architecture

```mermaid
graph TD
    subgraph C++ Quantitative Engine ["functions/ (C++ DLLs)"]
        M[MathLib.dll] --> Main[main.exe]
        YTM[calculateYTMYield.dll] --> Main
        ZC[calculateZeroCouponYield.dll] --> Main
        YC[calculateYieldCurve.dll (Upcoming)] -.-> Main
    end

    subgraph Python Strategy & Research ["Algorithmic Trading & ML"]
        Data[S&P 500 & Yahoo Finance Data] --> Feat[Feature Engineering & Technical Indicators]
        Feat --> FF[Fama-French 5-Factor Rolling Betas]
        FF --> ML[K-Means Clustering]
        ML --> Opt[Efficient Frontier & Sharpe Optimization]
    end
```

### Current Implemented Components

#### 1. C++ High-Performance Dynamic Link Libraries (`functions/`)
* **`MathLib.dll` ([MathLib.h](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/MathLib.h) / [MathLib.cpp](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/MathLib.cpp)):**
  - Basic math arithmetic.
  - Present Value of Geometric Annuities with growth and discount rate handling.
* **`calculateYTMYield.dll` ([calculateYTMYield.h](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/calculateYTMYield.h) / [calculateYTMYield.cpp](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/calculateYTMYield.cpp)):**
  - Analytical bond pricing for coupon-bearing bonds with arbitrary payment frequencies (annual, semi-annual, quarterly).
  - High-precision numerical Yield to Maturity (YTM) solver using the **Newton-Raphson** root-finding method.
* **`calculateZeroCouponYield.dll` ([calculateZeroCouponYield.h](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/calculateZeroCouponYield.h) / [calculateZeroCouponYield.cpp](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/calculateZeroCouponYield.cpp)):**
  - Analytical zero-coupon bond pricing.
  - Annual compounding spot yield solver: $y = (F/P)^{1/T} - 1$.
  - Continuously compounded spot yield solver: $y_{\text{cont}} = \ln(F/P) / T$.
* **`main.exe` ([main.cpp](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/main.cpp)):**
  - Harness executable dynamically linking all three DLLs and verifying calculations.
* **Build Automation ([build.bat](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/build.bat) & [Makefile](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions/Makefile)):**
  - Single-click compiler automation with `g++` on Windows.

#### 2. Machine Learning Trading Strategy ([Algorithmic_Trading_Machine_Learning_Quant_Strategies.ipynb](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/Algorithmic_Trading_Machine_Learning_Quant_Strategies.ipynb))
* S&P 500 multi-asset automated historical data pipeline.
* Technical indicator extraction (Garman-Klass Volatility, RSI, Bollinger Bands, ATR, MACD, Dollar Volume).
* Fama-French 5-factor regression and rolling factor betas calculation via `statsmodels.regression.rolling.RollingOLS`.
* K-Means clustering of assets across rolling historical monthly cross-sections.
* Maximum Sharpe Ratio portfolio construction via `PyPortfolioOpt` and Efficient Frontier optimization.

---

### Planned / Upcoming Components

1. **Full Yield Curve Construction & Bootstrapping (`calculateYieldCurve.dll`):**
   - Bootstrapping zero rates from a collection of coupon bonds and money market instruments.
   - Parametric term structure modeling (Nelson-Siegel and Nelson-Siegel-Svensson models).
   - Cubic Spline and Linear yield interpolation across tenors.
2. **Stationarity & Statistical Arbitrage Suite (`augmentedDickyFuller`):**
   - Augmented Dickey-Fuller (ADF) test implementation for mean-reversion detection.
   - Engle-Granger two-step cointegration testing for pairs trading.
3. **C++ & Python Bindings (ctypes / pybind11):**
   - Direct execution of C++ DLL pricing engines inside Python trading loops for ultra-fast backtesting.

---

## User Guide & How to Run

### 1. Building & Running the C++ Quantitative DLLs

#### Prerequisites
* Windows OS with GCC / MinGW (`g++`) installed and present in your system `PATH`.

#### Step-by-Step Execution
1. Open PowerShell or Command Prompt.
2. Navigate to the `functions` folder:
   ```cmd
   cd "d:\Documents\_MyStuff\Projects\quant_developer_learning\Algorithmic Trading Machine Learning Strategies\functions"
   ```
3. Run the automated build script:
   ```cmd
   build.bat
   ```
   *(Alternatively, if you have Make installed, you can run `make` or `make run`).*

4. **Expected Output:**
   The script compiles `MathLib.dll`, `calculateYTMYield.dll`, `calculateZeroCouponYield.dll`, links `main.exe`, and runs the demo:
   ```text
   =====================================================
          QUANTITATIVE FINANCE DLL INTEGRATION DEMO     
   =====================================================

   --- [1] MathLib.dll ---
   Basic Addition (15 + 27): 42
   Present Value of Geometric Annuity: $7920.53

   --- [2] calculateYTMYield.dll (Coupon Bond YTM) ---
   Bond Parameters: Face Value=$1000.00, Coupon=5.00% (Semi-Annual), Periods=20, Market Price=$960.00
   Calculated YTM (Yield to Maturity): 5.5260%
   Verification Price using YTM:      $960.00

   --- [3] calculateZeroCouponYield.dll (Zero-Coupon Spot Yield) ---
   Zero-Coupon Parameters: Face Value=$1000.00, Price=$783.53, Maturity=5.00 years
   Annual Compounding Spot Yield:       4.9999%
   Continuous Compounding Spot Yield:   4.8789%
   Verification Price using Spot Yield: $783.53
   ```

---

### 2. Running the Python Machine Learning Trading Strategy

#### Prerequisites
* Python 3.11+ with the project virtual environment `.venv`.

#### Step-by-Step Setup
1. Activate the project virtual environment:
   ```powershell
   cd "d:\Documents\_MyStuff\Projects\quant_developer_learning\Algorithmic Trading Machine Learning Strategies"
   .\.venv\Scripts\Activate.ps1
   ```
2. Ensure all dependencies are installed:
   ```powershell
   pip install -r requirements.txt
   ```
3. Open the Jupyter Notebook:
   * Open [Algorithmic_Trading_Machine_Learning_Quant_Strategies.ipynb](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/Algorithmic_Trading_Machine_Learning_Quant_Strategies.ipynb) in your IDE.
   * Select Kernel: Choose **`.venv (Python 3.11)`**.
   * Run cells sequentially (`Shift + Enter`).

---

### 3. How to Add a New C++ DLL Component

To maintain clean modular architecture, follow the project's standard 3-step pattern when adding any new C++ calculation module:

#### Step 1: Create Header `MyComponent.h`
```cpp
#ifndef MYCOMPONENT_H
#define MYCOMPONENT_H

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef MYCOMPONENT_EXPORTS
        #define MYCOMPONENT_API __declspec(dllexport)
    #else
        #define MYCOMPONENT_API __declspec(dllimport)
    #endif
#else
    #define MYCOMPONENT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

MYCOMPONENT_API double my_quant_function(double input_param);

#ifdef __cplusplus
}
#endif

#endif // MYCOMPONENT_H
```

#### Step 2: Create Implementation `MyComponent.cpp`
```cpp
#include "MyComponent.h"

MYCOMPONENT_API double my_quant_function(double input_param) {
    return input_param * 2.0;
}
```

#### Step 3: Add to `build.bat`
```bat
g++ -DMYCOMPONENT_EXPORTS -shared -o MyComponent.dll MyComponent.cpp -Wl,--out-implib,libMyComponent.a
g++ -o main.exe main.cpp -L. -lMathLib -lcalculateYTMYield -lcalculateZeroCouponYield -lMyComponent
```

---

## Directory Structure

```text
quant_developer_learning/
├── README.md                                           # Root documentation and user guide
├── Quant Developer Roadmap - 12 Week Plan.pdf          # Quant developer learning syllabus
├── quant_developer_roadmap_tracker.html                # Interactive progress tracking dashboard
├── .venv/                                              # Python virtual environment
├── Algorithmic Trading Machine Learning Strategies/
│   ├── Algorithmic_Trading_Machine_Learning_Quant_Strategies.ipynb # ML Quant Trading Notebook
│   ├── requirements.txt                                # Python package dependencies
│   ├── sentiment_data.csv                              # Sentiment dataset
│   ├── simulated_5min_data.csv                         # Intraday 5-min simulated pricing
│   ├── simulated_daily_data.csv                        # Daily simulated pricing
│   └── functions/                                      # C++ DLL Subsystem
│       ├── MathLib.h / MathLib.cpp                     # General financial math DLL
│       ├── calculateYTMYield.h / .cpp                  # Coupon bond YTM solver DLL
│       ├── calculateZeroCouponYield.h / .cpp           # Zero-coupon spot yield DLL
│       ├── calculateYieldCurve.h                       # Forwarding header alias
│       ├── main.cpp                                    # Integration driver application
│       ├── build.bat                                   # Windows automated build script
│       └── Makefile                                    # Makefile for GNU Make
└── augmentedDickyFuller/                               # ADF statistical testing module (Planned)
```
