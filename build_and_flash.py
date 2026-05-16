import os
import sys
import subprocess

def run_command(command):
    try:
        subprocess.check_call(command, shell=True)
        return True
    except subprocess.CalledProcessError:
        return False

def main():
    print("=== CRSF Backpack Bridge Build & Flash Tool ===")

    # Check for ESP-IDF
    if "IDF_PATH" not in os.environ:
        print("\n[!] ESP-IDF environment not found.")
        print("Please run this script from the 'ESP-IDF Command Prompt'")
        print("or run 'export.bat' from your ESP-IDF installation folder.")
        input("\nPress Enter to exit...")
        sys.exit(1)

    print("\n[1] Building firmware...")
    if not run_command("idf.py build"):
        print("\n[!] Build failed!")
        input("Press Enter to exit...")
        sys.exit(1)

    print("\n[2] Flashing to device...")
    print("Available ports will be detected automatically.")
    if not run_command("idf.py flash monitor"):
        print("\n[!] Flashing failed!")
        input("Press Enter to exit...")
        sys.exit(1)

if __name__ == "__main__":
    main()
