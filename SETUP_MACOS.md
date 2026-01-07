# Manual Setup Guide for macOS

Follow these steps to set up the development environment and build the Binance Dashboard.

## Step 1: Install vcpkg Package Manager

```bash
# Navigate to your home directory
cd ~

# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git

# Navigate to vcpkg directory
cd vcpkg

# Bootstrap vcpkg (this compiles the package manager)
./bootstrap-vcpkg.sh

# Set environment variables (add these to your ~/.zshrc or ~/.bash_profile)
export VCPKG_ROOT=~/vcpkg
export PATH=$VCPKG_ROOT:$PATH

# Apply the environment variables to current session
source ~/.zshrc  # or source ~/.bash_profile
```

## Step 2: Install Required Dependencies

```bash
# Install all required packages (this may take 10-20 minutes)
~/vcpkg/vcpkg install websocketpp nlohmann-json openssl boost-system boost-thread boost-chrono boost-random

# Verify installation
~/vcpkg/vcpkg list | grep -E "(websocketpp|nlohmann-json|openssl|boost)"
```

## Step 3: Install CMake (if not already installed)

```bash
# Using Homebrew (install Homebrew first if needed: https://brew.sh)
brew install cmake

# Verify CMake installation
cmake --version
```

## Step 4: Build the Project

```bash
# Navigate to the project directory
cd binance_dashboard

# Create and enter build directory
mkdir build && cd build

# Configure the project with vcpkg integration
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release

# Build the project
make -j4

# Verify the executable was created
ls -la binance_dashboard
```

## Step 5: Run the Application

```bash
# From the build directory
./binance_dashboard
```

## Troubleshooting

### If vcpkg bootstrap fails:
```bash
# Make sure you have Xcode command line tools
xcode-select --install

# Try again
./bootstrap-vcpkg.sh
```

### If cmake can't find vcpkg:
```bash
# Make sure VCPKG_ROOT is set correctly
echo $VCPKG_ROOT
# Should show: /Users/yourusername/vcpkg

# If not set, export it manually:
export VCPKG_ROOT=~/vcpkg
```

### If dependencies fail to install:
```bash
# Update vcpkg
cd ~/vcpkg
git pull

# Try installing one by one
./vcpkg install openssl
./vcpkg install nlohmann-json
./vcpkg install websocketpp
./vcpkg install boost-system boost-thread boost-chrono boost-random
```

### If build fails with "package not found":
```bash
# Clean and reconfigure
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
make -j4
```

## Expected Output

When successful, you should see:
```
🚀 Building Binance Dashboard...
=================================
✅ Found vcpkg at: /Users/yourusername/vcpkg
🔍 Checking dependencies...
✅ Found: websocketpp
✅ Found: nlohmann-json
✅ Found: openssl
✅ Found: boost-system
✅ Found: boost-thread
✅ Found: boost-chrono
✅ Found: boost-random
✅ All dependencies are installed
📁 Creating build directory...
⚙️  Configuring with CMake...
🔨 Building project...
🎉 Build successful!
```

The application will then connect to Binance and display real-time cryptocurrency data!
