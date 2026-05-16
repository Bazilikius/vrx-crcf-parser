import os
import sys
import subprocess
import glob

def find_idf_path():
    # Common search paths
    search_paths = [
        os.path.join(os.environ.get('USERPROFILE', ''), 'esp', 'esp-idf'),
        'C:/Espressif/frameworks'
    ]

    for path in search_paths:
        if os.path.exists(path):
            if os.path.exists(os.path.join(path, 'export.bat')):
                return path
            # Check subdirectories (for C:\Espressif\frameworks\esp-idf-vX.X)
            subdirs = glob.glob(os.path.join(path, 'esp-idf*'))
            for subdir in subdirs:
                if os.path.exists(os.path.join(subdir, 'export.bat')):
                    return subdir
    return None

def run_with_idf(command, idf_path=None):
    if idf_path:
        # On Windows, we can chain the export.bat with our command
        full_command = f'"{os.path.join(idf_path, "export.bat")}" && {command}'
    else:
        full_command = command

    try:
        subprocess.check_call(full_command, shell=True)
        return True
    except subprocess.CalledProcessError:
        return False

def main():
    print("=== CRSF Backpack Bridge Build & Flash Tool ===")

    idf_path = None
    if "IDF_PATH" not in os.environ:
        print("[INFO] IDF_PATH not found in environment. Searching common locations...")
        idf_path = find_idf_path()
        if idf_path:
            print(f"[INFO] Found ESP-IDF at: {idf_path}")
        else:
            print("\n[!] ESP-IDF environment not found.")
            print("Please enter the full path to your ESP-IDF installation folder")
            print("(e.g. C:\\Espressif\\frameworks\\esp-idf-v5.1)")
            path_input = input("Path: ").strip()
            if path_input and os.path.exists(os.path.join(path_input, 'export.bat')):
                idf_path = path_input
            else:
                print("[ERROR] Could not find export.bat at that location.")
                input("\nPress Enter to exit...")
                sys.exit(1)

    print("\n[1] Building firmware...")
    if not run_with_idf("idf.py build", idf_path):
        print("\n[!] Build failed!")
        input("Press Enter to exit...")
        sys.exit(1)

    print("\n[2] Flashing to device...")
    if not run_with_idf("idf.py flash monitor", idf_path):
        print("\n[!] Flashing failed!")
        input("Press Enter to exit...")
        sys.exit(1)

if __name__ == "__main__":
    main()
