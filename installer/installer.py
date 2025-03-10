import os
import sys
import subprocess
import platform
import shutil

def ensure_python_dependencies():
    """
    Verifica y gestiona las dependencias de Python necesarias
    para ejecutar la aplicación correctamente
    """
    required_packages = ["requests", "pyserial", "pillow"]
    missing_packages = []
    
    # Verificar qué paquetes faltan
    try:
        import requests
    except ImportError:
        missing_packages.append("requests")
        
    try:
        import serial
    except ImportError:
        missing_packages.append("pyserial")
        
    try:
        import PIL
    except ImportError:
        missing_packages.append("pillow")
    
    # Si no faltan paquetes, continuar
    if not missing_packages:
        return True
    
    print(f"Faltan dependencias Python: {', '.join(missing_packages)}")
    print("Intentando instalar automáticamente...")
    
    # Intentar instalar con pip
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "--user"] + required_packages)
        print("Dependencias instaladas correctamente. Reiniciando aplicación...")
        
        # Reiniciar la aplicación para usar las nuevas dependencias
        os.execv(sys.executable, [sys.executable] + sys.argv)
        return True
    except subprocess.CalledProcessError:
        print("No se pudo instalar con pip. Intentando con el gestor de paquetes del sistema...")
        
        # Intentar con el gestor de paquetes del sistema
        if platform.system() == "Linux":
            installed = False
            
            # Debian/Ubuntu
            if shutil.which("apt"):
                packages = ["python3-requests", "python3-serial", "python3-pil", "python3-tk"]
                try:
                    print("Instalando paquetes con apt...")
                    subprocess.run(["pkexec", "apt", "install", "-y"] + packages, check=True)
                    installed = True
                except (subprocess.CalledProcessError, FileNotFoundError):
                    pass
                    
            # Fedora/RHEL
            if not installed and shutil.which("dnf"):
                packages = ["python3-requests", "python3-pyserial", "python3-pillow", "python3-tkinter"]
                try:
                    print("Instalando paquetes con dnf...")
                    subprocess.run(["pkexec", "dnf", "install", "-y"] + packages, check=True)
                    installed = True
                except (subprocess.CalledProcessError, FileNotFoundError):
                    pass
                    
            # Arch Linux
            if not installed and shutil.which("pacman"):
                packages = ["python-requests", "python-pyserial", "python-pillow", "python-tk"]
                try:
                    print("Instalando paquetes con pacman...")
                    subprocess.run(["pkexec", "pacman", "-S", "--noconfirm"] + packages, check=True)
                    installed = True
                except (subprocess.CalledProcessError, FileNotFoundError):
                    pass
            
            if installed:
                print("Dependencias instaladas con el gestor de paquetes del sistema.")
                print("Reiniciando aplicación...")
                os.execv(sys.executable, [sys.executable] + sys.argv)
                return True
        
        # Si llegamos aquí, no se pudo instalar automáticamente
        print("\nERROR: No se pudieron instalar las dependencias automáticamente.")
        print("Por favor, instale manualmente los siguientes paquetes Python:")
        print("  - requests (para comunicación HTTP)")
        print("  - pyserial (para comunicación serial)")
        print("  - pillow (para procesamiento de imágenes)")
        print("  - tkinter (para la interfaz gráfica)")
        print("\nComando para instalar con pip:")
        print(f"  {sys.executable} -m pip install --user requests pyserial pillow")
        print("\nO use el gestor de paquetes de su distribución.")
        
        # Pedir confirmación antes de continuar
        try:
            input("\nPresione Enter para salir...")
        except:
            pass
        sys.exit(1)

# Ejecutar verificación de dependencias antes de importar otros módulos
if __name__ == "__main__":
    ensure_python_dependencies()


import os
import sys
import subprocess
import tempfile
import shutil
import platform
import threading
import re
import time
import tkinter as tk
from tkinter import ttk, messagebox
import requests
import zipfile
import io

class ARG_OSCI_Installer:
    def __init__(self, root):
        self.root = root
        self.root.title("ARG_OSCI Firmware Installer")
        self.root.geometry("800x600")
        
        self.temp_dir = tempfile.mkdtemp()
        self.firmware_path = os.path.join(self.temp_dir, "firmware")
        self.idf_path = os.path.join(self.temp_dir, "esp-idf")
        self.idf_tools_path = os.path.join(self.temp_dir, "esp-idf-tools")
        
        self.setup_ui()
    
    def setup_ui(self):
        # Create tabs
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill='both', expand=True, padx=10, pady=10)
        
        config_frame = ttk.Frame(notebook)
        install_frame = ttk.Frame(notebook)
        path_frame = ttk.Frame(notebook)
        monitor_frame = ttk.Frame(notebook)  # New tab for serial monitor
        
        notebook.add(config_frame, text="Configuration")
        notebook.add(path_frame, text="Installation Paths")
        notebook.add(install_frame, text="Installation")
        notebook.add(monitor_frame, text="Serial Monitor")
        
        # Configuration tab
        ttk.Label(config_frame, text="WiFi Configuration", font=("Arial", 12, "bold")).grid(row=0, column=0, sticky="w", pady=10)
        
        # [Rest of the configuration UI remains the same]
        ttk.Label(config_frame, text="SSID:").grid(row=1, column=0, sticky="w", padx=20)
        self.ssid_var = tk.StringVar(value="ESP32_AP")
        ttk.Entry(config_frame, textvariable=self.ssid_var, width=30).grid(row=1, column=1, sticky="w")
        
        ttk.Label(config_frame, text="Password:").grid(row=2, column=0, sticky="w", padx=20)
        self.password_var = tk.StringVar(value="password123")
        ttk.Entry(config_frame, textvariable=self.password_var, width=30).grid(row=2, column=1, sticky="w")
        
        ttk.Label(config_frame, text="Hardware Configuration", font=("Arial", 12, "bold")).grid(row=3, column=0, sticky="w", pady=10)
        
        self.adc_mode_var = tk.StringVar(value="internal")
        ttk.Radiobutton(config_frame, text="Internal ADC", variable=self.adc_mode_var, value="internal").grid(row=4, column=0, sticky="w", padx=20)
        ttk.Radiobutton(config_frame, text="External ADC", variable=self.adc_mode_var, value="external").grid(row=4, column=1, sticky="w")
        
        ttk.Label(config_frame, text="Serial Port", font=("Arial", 12, "bold")).grid(row=5, column=0, sticky="w", pady=10)
        
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(config_frame, textvariable=self.port_var, width=30)
        self.port_combo.grid(row=6, column=0, columnspan=2, sticky="w", padx=20)
        
        ttk.Button(config_frame, text="Refresh Ports", command=self.refresh_ports).grid(row=6, column=2, padx=5)
        
        # Installation paths tab
        ttk.Label(path_frame, text="Installation Paths", font=("Arial", 12, "bold")).grid(row=0, column=0, sticky="w", pady=10)
        
        ttk.Label(path_frame, text="Firmware Path:").grid(row=1, column=0, sticky="w", padx=20)
        self.firmware_path_var = tk.StringVar(value=os.path.join(self.temp_dir, "firmware"))
        ttk.Entry(path_frame, textvariable=self.firmware_path_var, width=40).grid(row=1, column=1, sticky="w")
        ttk.Button(path_frame, text="Browse", command=lambda: self.select_directory("firmware")).grid(row=1, column=2, padx=5)
        
        ttk.Label(path_frame, text="ESP-IDF Path:").grid(row=2, column=0, sticky="w", padx=20)
        self.idf_path_var = tk.StringVar(value=os.path.join(self.temp_dir, "esp-idf"))
        ttk.Entry(path_frame, textvariable=self.idf_path_var, width=40).grid(row=2, column=1, sticky="w")
        ttk.Button(path_frame, text="Browse", command=lambda: self.select_directory("esp-idf")).grid(row=2, column=2, padx=5)
        
        ttk.Label(path_frame, text="ESP-IDF Tools Path:").grid(row=3, column=0, sticky="w", padx=20)
        self.idf_tools_path_var = tk.StringVar(value=os.path.join(self.temp_dir, "esp-idf-tools"))
        ttk.Entry(path_frame, textvariable=self.idf_tools_path_var, width=40).grid(row=3, column=1, sticky="w")
        ttk.Button(path_frame, text="Browse", command=lambda: self.select_directory("tools")).grid(row=3, column=2, padx=5)
        
        ttk.Button(path_frame, text="Check for Existing ESP-IDF", command=self.find_existing_esp_idf).grid(row=4, column=1, pady=10)
        
        # Installation tab
        # [Rest of the installation UI remains the same]
        self.progress = ttk.Progressbar(install_frame, orient="horizontal", length=500, mode="determinate")
        self.progress.pack(pady=20, padx=20, fill="x")
        
        self.status_text = tk.Text(install_frame, height=15, width=70)
        self.status_text.pack(pady=10, padx=20, fill="both", expand=True)
        
    
        # Serial Monitor tab
        ttk.Label(monitor_frame, text="Serial Monitor", font=("Arial", 12, "bold")).pack(anchor="w", pady=10)

        # Monitor controls frame
        controls_frame = ttk.Frame(monitor_frame)
        controls_frame.pack(fill="x", pady=10)

        # Port selection
        ttk.Label(controls_frame, text="Port:").pack(side="left", padx=5)
        self.monitor_port_var = tk.StringVar()
        self.monitor_port_combo = ttk.Combobox(controls_frame, textvariable=self.monitor_port_var, width=20)
        self.monitor_port_combo.pack(side="left", padx=5)

        # Use the same port as the config tab
        self.monitor_port_var.trace_add("write", lambda *args: self.port_var.set(self.monitor_port_var.get()))
        self.port_var.trace_add("write", lambda *args: self.monitor_port_var.set(self.port_var.get()))

        ttk.Button(controls_frame, text="Refresh Ports", command=self.refresh_monitor_ports).pack(side="left", padx=5)
        ttk.Button(controls_frame, text="Launch IDF Monitor", command=self.launch_idf_monitor).pack(side="left", padx=10)

        # Description text
        description_frame = ttk.Frame(monitor_frame)
        description_frame.pack(fill="x", pady=10)

        ttk.Label(description_frame, text="IDF Monitor provides enhanced debugging features:", 
                  font=("Arial", 10, "bold")).pack(anchor="w", padx=5)

        features = [
            "• Colorized output of log messages sent by the ESP32",
            "• Automatic decoding of addresses in stack traces",
            "• Commands to reset the ESP32, display memory/registers, and more",
            "• Filtering of log output for specific components",
            "• Parsing and highlighting errors in GCC build output"
        ]

        for feature in features:
            ttk.Label(description_frame, text=feature).pack(anchor="w", padx=20)

        ttk.Label(description_frame, text="Press Ctrl+] to exit the monitor when finished.", 
                  font=("Arial", 9, "italic")).pack(anchor="w", padx=5, pady=10)

        # Instructions frame
        instructions_frame = ttk.Frame(monitor_frame)
        instructions_frame.pack(fill="x", pady=10)

        ttk.Label(instructions_frame, text="Common IDF Monitor Commands:", 
                  font=("Arial", 10, "bold")).pack(anchor="w", padx=5)

        commands = [
            "• Ctrl+T, Ctrl+R: Reset ESP32",
            "• Ctrl+T, Ctrl+F: Build and flash the project",
            "• Ctrl+T, Ctrl+H: Display all help commands",
            "• Ctrl+T, Ctrl+X: Exit the monitor"
        ]

        for command in commands:
            ttk.Label(instructions_frame, text=command).pack(anchor="w", padx=20)

        # Button frame at the bottom of the window
        button_frame = ttk.Frame(self.root)
        button_frame.pack(fill="x", padx=10, pady=10)
        
        ttk.Button(button_frame, text="Install", command=self.start_installation).pack(side="right", padx=5)
        ttk.Button(button_frame, text="Configure & Flash", command=self.configure_and_flash).pack(side="right", padx=5)
        ttk.Button(button_frame, text="Cancel", command=self.root.destroy).pack(side="right", padx=5)
        # Initialize ports
        self.refresh_ports()
        self.serial_connected = False
        self.serial_thread = None
        self.serial_port = None
        self.stop_monitor = threading.Event()
    

    def refresh_monitor_ports(self):
        """Refresh the monitor serial ports"""
        ports = self.get_serial_ports()
        self.monitor_port_combo['values'] = ports
        if ports:
            self.monitor_port_var.set(ports[0])

    def launch_idf_monitor(self):
        """Launch IDF's built-in monitor tool in a new terminal window"""
        port = self.monitor_port_var.get()
    
        if not port:
            messagebox.showerror("Error", "Please select a serial port")
            return
    
        # Check if port exists and is accessible
        if not os.path.exists(port):
            messagebox.showerror("Port Not Found", f"Serial port {port} not found")
            return
    
        if not self.check_port_permission(port):
            return
    
        self.log(f"Launching ESP-IDF monitor on port {port}...")
    
        # Update paths from UI
        self.firmware_path = self.firmware_path_var.get()
        self.idf_path = self.idf_path_var.get()
        self.idf_tools_path = self.idf_tools_path_var.get()
    
        # Check if ESP-IDF exists
        if not os.path.exists(self.idf_path) or not os.path.isfile(
            os.path.join(self.idf_path, "export.sh" if platform.system() != "Windows" else "export.bat")
        ):
            messagebox.showerror("ESP-IDF Missing", "ESP-IDF not found. Please install it first.")
            return
    
        if not os.path.exists(self.firmware_path):
            messagebox.showerror("Firmware Missing", "Firmware path not found. Please download or build the firmware first.")
            return
    
        # Set environment variables
        os.environ["IDF_TOOLS_PATH"] = self.idf_tools_path
    
        # Create and launch appropriate script based on platform
        if platform.system() == "Windows":
            export_script = os.path.join(self.idf_path, "export.bat")
            with tempfile.NamedTemporaryFile(suffix='.bat', delete=False) as temp:
                temp_script = temp.name
                temp.write(f'@echo off\r\n'
                          f'echo Launching ESP-IDF monitor on {port}...\r\n'
                          f'call "{export_script}"\r\n'
                          f'cd "{self.firmware_path}"\r\n'
                          f'echo Press Ctrl+] to exit the monitor\r\n'
                          f'idf.py -p {port} monitor\r\n'
                          f'echo Monitor closed.\r\n'
                          f'pause\r\n'.encode())
    
            # Launch in a new cmd window
            self.log(f"Opening ESP-IDF monitor in a new terminal window")
            subprocess.Popen(f'start cmd /k "{temp_script}"', shell=True)
    
        else:  # Linux
            export_script = os.path.join(self.idf_path, "export.sh")
            with tempfile.NamedTemporaryFile(mode='w+', suffix='.sh', delete=False) as temp:
                temp_script = temp.name
                temp.write(f'''#!/bin/bash
    echo "Launching ESP-IDF monitor on {port}..."
    source "{export_script}"
    cd "{self.firmware_path}"
    echo "Press Ctrl+] to exit the monitor"
    idf.py -p {port} monitor
    echo "Monitor closed."
    read -p "Press Enter to close this window"
    ''')
    
            os.chmod(temp_script, 0o755)
    
            # Try to detect and use an available terminal
            terminal_found = False
            for terminal in ["x-terminal-emulator", "gnome-terminal", "konsole", "xterm"]:
                if shutil.which(terminal):
                    if terminal == "gnome-terminal":
                        subprocess.Popen(f'{terminal} -- {temp_script}', shell=True)
                    elif terminal == "konsole":
                        subprocess.Popen(f'{terminal} --noclose -e {temp_script}', shell=True)
                    elif terminal == "x-terminal-emulator":
                        subprocess.Popen(f'{terminal} -e {temp_script}', shell=True)
                    else:
                        subprocess.Popen(f'{terminal} -hold -e {temp_script}', shell=True)
                    self.log(f"Opening ESP-IDF monitor in a new {terminal} window")
                    terminal_found = True
                    break
                    
            if not terminal_found:
                messagebox.showerror("Error", "Could not find a suitable terminal emulator")
                os.unlink(temp_script)
                return
    
        messagebox.showinfo("Monitor Launched", 
                            "ESP-IDF monitor has been launched in a separate terminal window.\n\n"
                            "Press Ctrl+] to exit when finished.")

    def select_directory(self, dir_type):
        """Open a dialog to select a directory"""
        from tkinter import filedialog
        directory = filedialog.askdirectory()
        if directory:
            if dir_type == "firmware":
                self.firmware_path_var.set(directory)
            elif dir_type == "esp-idf":
                self.idf_path_var.set(directory)
            elif dir_type == "tools":
                self.idf_tools_path_var.set(directory)
    
    def find_existing_esp_idf(self):
        """Check for existing ESP-IDF installations"""
        # First check environment variables
        env_locations = []
        if os.environ.get("IDF_PATH"):
            env_locations.append(os.environ.get("IDF_PATH"))
        if os.environ.get("ESP_IDF_PATH"):
            env_locations.append(os.environ.get("ESP_IDF_PATH"))
                
        # Get user home directory correctly for each platform
        home_dir = os.path.expanduser("~")
        username = os.path.basename(home_dir)
            
        # Common installation locations
        possible_locations = [
            # Environment variable locations
            *env_locations,
            # Linux locations
            os.path.join(home_dir, "esp", "esp-idf"),
            os.path.join(home_dir, ".espressif", "esp-idf"),
            os.path.join("/opt", "esp", "esp-idf"),
            os.path.join("/usr", "local", "esp-idf"),
            # Windows locations
            os.path.join("C:\\", "esp", "esp-idf"),
            os.path.join("C:\\", "Users", username, "esp", "esp-idf"),
            os.path.join("C:\\", "Espressif", "frameworks", "esp-idf-v5.3.1")
        ]
            
        for location in possible_locations:
            if location and os.path.exists(location) and os.path.isfile(os.path.join(location, "export.sh" if platform.system() != "Windows" else "export.bat")):
                self.idf_path_var.set(location)
                    
                # Check for tools path
                tools_env = os.environ.get("IDF_TOOLS_PATH")
                if tools_env and os.path.exists(tools_env):
                    self.idf_tools_path_var.set(tools_env)
                else:
                    # Common tools paths relative to ESP-IDF
                    for tools_dir in [os.path.join(os.path.dirname(location), "esp-idf-tools"),
                                     os.path.join(home_dir, ".espressif")]:
                        if os.path.exists(tools_dir):
                            self.idf_tools_path_var.set(tools_dir)
                            break
                    
                messagebox.showinfo("ESP-IDF Found", f"Found existing ESP-IDF installation at:\n{location}")
                return
            
        # If automatic detection failed, ask the user to manually locate ESP-IDF
        manual_response = messagebox.askquestion("ESP-IDF Not Found", 
                                               "Automatic detection couldn't find ESP-IDF installation.\n\n"
                                               "Would you like to manually locate a directory to search for ESP-IDF?")
        if manual_response == 'yes':
            from tkinter import filedialog
            selected_dir = filedialog.askdirectory(title="Select Directory to Search for ESP-IDF")
            
            if selected_dir:
                self.log("Searching for ESP-IDF installations, this may take a moment...")
                # Find all ESP-IDF installations in the selected directory up to 4 levels deep
                esp_idf_installations = self.find_esp_idf_recursively(selected_dir, max_depth=4)
                
                if esp_idf_installations:
                    # Sort installations by version, prioritizing 5.3.1 or newer
                    esp_idf_installations.sort(key=self.get_esp_idf_version, reverse=True)
                    best_match = esp_idf_installations[0]
                    
                    self.idf_path_var.set(best_match)
                    
                    # Try to find tools path near the ESP-IDF directory
                    home_dir = os.path.expanduser("~")
                    for tools_dir in [os.path.join(os.path.dirname(best_match), "esp-idf-tools"),
                                     os.path.join(home_dir, ".espressif")]:
                        if os.path.exists(tools_dir):
                            self.idf_tools_path_var.set(tools_dir)
                            break
                    
                    version_info = self.get_esp_idf_version_str(best_match)
                    messagebox.showinfo("ESP-IDF Found", 
                                       f"Found ESP-IDF {version_info} at:\n{best_match}\n\n"
                                       f"Total installations found: {len(esp_idf_installations)}")
                    return
                else:
                    messagebox.showerror("ESP-IDF Not Found", 
                                        "No ESP-IDF installations found in the selected directory.")
        
        messagebox.showinfo("ESP-IDF Not Found", 
                           "No existing ESP-IDF installation found. It will be downloaded during installation.")    
    
    def find_esp_idf_recursively(self, directory, max_depth=4, current_depth=0):
        """
        Recursively search for ESP-IDF installations in a directory
        Returns a list of paths to ESP-IDF installations
        """
        if current_depth > max_depth:
            return []
        
        results = []
        export_script = "export.bat" if platform.system() == "Windows" else "export.sh"
        
        # Check if this directory is an ESP-IDF installation
        if os.path.isfile(os.path.join(directory, export_script)):
            results.append(directory)
        
        # Search subdirectories
        try:
            for item in os.listdir(directory):
                full_path = os.path.join(directory, item)
                if os.path.isdir(full_path):
                    results.extend(self.find_esp_idf_recursively(full_path, max_depth, current_depth + 1))
        except (PermissionError, OSError):
            # Skip directories we can't access
            pass
        
        return results
    
    def get_esp_idf_version(self, idf_path):
        """
        Get the version number of an ESP-IDF installation for sorting
        Returns a tuple of (major, minor, patch) or default value if version can't be determined
        """
        # Method 1: Try to get version from directory name
        dir_name = os.path.basename(idf_path)
        if dir_name.startswith("esp-idf-v"):
            try:
                version_str = dir_name[len("esp-idf-v"):]
                return tuple(map(int, version_str.split('.')))
            except (ValueError, IndexError):
                pass
        
        # Method 2: Try to get version from version.txt
        version_file = os.path.join(idf_path, "version.txt")
        if os.path.exists(version_file):
            try:
                with open(version_file, 'r') as f:
                    version_str = f.read().strip()
                    if version_str.startswith('v'):
                        version_str = version_str[1:]
                    return tuple(map(int, version_str.split('.')[:3]))
            except (ValueError, IndexError, IOError):
                pass
        
        # Method 3: Try to get version from CMakeLists.txt
        cmake_file = os.path.join(idf_path, "CMakeLists.txt")
        if os.path.exists(cmake_file):
            try:
                with open(cmake_file, 'r') as f:
                    content = f.read()
                    version_match = re.search(r'set\s*\(\s*IDF_VERSION_MAJOR\s+(\d+)\s*\)', content)
                    if version_match:
                        major = int(version_match.group(1))
                        minor_match = re.search(r'set\s*\(\s*IDF_VERSION_MINOR\s+(\d+)\s*\)', content)
                        minor = int(minor_match.group(1)) if minor_match else 0
                        patch_match = re.search(r'set\s*\(\s*IDF_VERSION_PATCH\s+(\d+)\s*\)', content)
                        patch = int(patch_match.group(1)) if patch_match else 0
                        return (major, minor, patch)
            except (ValueError, IndexError, IOError):
                pass
        
        # Default: Return a low version number for unknown versions
        # This ensures known versions are prioritized
        return (0, 0, 0)
    
    def get_esp_idf_version_str(self, idf_path):
        """Get a string representation of the ESP-IDF version"""
        version = self.get_esp_idf_version(idf_path)
        if version == (0, 0, 0):
            return "unknown version"
        return f"v{version[0]}.{version[1]}.{version[2]}"
    
    def run_installation(self):
        """Run the complete installation process with updated paths"""
        try:
            self.progress["value"] = 0
            self.log("Starting installation...")
            
            # Update paths from UI
            self.firmware_path = self.firmware_path_var.get()
            self.idf_path = self.idf_path_var.get()
            self.idf_tools_path = self.idf_tools_path_var.get()
            
            # Download firmware
            self.progress["value"] = 10
            self.log("Downloading ARG_OSCI firmware...")
            self.download_firmware()
            
            # Check if ESP-IDF needs to be downloaded
            self.progress["value"] = 25
            esp_idf_exists = os.path.exists(self.idf_path) and os.path.isfile(
                os.path.join(self.idf_path, "export.sh" if platform.system() != "Windows" else "export.bat")
            )
            
            if esp_idf_exists:
                self.log(f"Using existing ESP-IDF at {self.idf_path}")
            else:
                self.log("ESP-IDF not found. Downloading ESP-IDF v5.3.1...")
                self.log("This may take a while (300MB+). It includes the compiler toolchain and libraries needed to build ESP32 firmware.")
                self.setup_esp_idf()
            
            # Configure firmware
            self.progress["value"] = 50
            self.log("Configuring firmware...")
            self.configure_firmware()
            
            # Build firmware
            self.progress["value"] = 70
            self.log("Building firmware...")
            self.build_firmware(clean_build=True)
            
            # Flash firmware
            self.progress["value"] = 90
            self.log("Flashing firmware to ESP32...")
            if self.flash_firmware():
                self.progress["value"] = 100
                self.log("Installation complete!")
                messagebox.showinfo("Success", "ARG_OSCI firmware has been successfully installed!")
            else:
                self.progress["value"] = 0
                self.log("Installation failed during flashing")
                messagebox.showerror("Installation Error", "Installation failed during flashing. Please check the logs for details.")
            
        except Exception as e:
            self.log(f"Error: {str(e)}")
            messagebox.showerror("Installation Error", str(e))
    
    def build_firmware(self, clean_build=False):
        """Build the firmware using ESP-IDF"""
        # Set up environment variables
        os.environ["IDF_TOOLS_PATH"] = self.idf_tools_path

        # Only clean the build directory when doing a full installation
        build_dir = os.path.join(self.firmware_path, "build")
        if clean_build and os.path.exists(build_dir):
            self.log("Cleaning previous build directory...")
            shutil.rmtree(build_dir)
            os.makedirs(build_dir, exist_ok=True)
        elif not os.path.exists(build_dir):
            os.makedirs(build_dir, exist_ok=True)

        # Create the git-data directory and needed files if they don't exist
        git_data_dir = os.path.join(build_dir, "CMakeFiles", "git-data")
        if not os.path.exists(git_data_dir):
            os.makedirs(git_data_dir, exist_ok=True)
            with open(os.path.join(git_data_dir, "head-ref"), 'w') as f:
                f.write("e51aed0a38d795ed5d5ba82c4aef7791929780a5\n")

        # For Linux, fix permissions
        if platform.system() != "Windows":
            self.check_and_fix_esp_idf_permissions()

        if platform.system() == "Windows":
            export_script = os.path.join(self.idf_path, "export.bat")
            # Create a temporary batch file to source export.bat and then run the build command
            with tempfile.NamedTemporaryFile(suffix='.bat', delete=False) as temp:
                temp_script = temp.name
                temp.write(f'call "{export_script}"\r\ncd "{self.firmware_path}"\r\nidf.py build\r\n'.encode())
            
            self.execute_command(temp_script)
            os.unlink(temp_script)
        else:
            export_script = os.path.join(self.idf_path, "export.sh")
            os.chmod(export_script, 0o755)  # Ensure the script is executable
            # Explicitly use bash to ensure 'source' command works
            self.execute_command(f'/bin/bash -c \'source "{export_script}" && cd "{self.firmware_path}" && idf.py build\'')
        
        self.log("Firmware build completed")

    def check_port_permission(self, port):
        """Check if the user has permission to access the specified port"""
        if platform.system() != "Windows" and not os.access(port, os.R_OK | os.W_OK):
            # Permission issue on Linux
            group = "dialout"  # Most common group for serial access on Linux
            username = os.getenv("USER")
    
            # Check if user is already in the group
            try:
                groups_output = subprocess.check_output(['groups', username], text=True)
                if group in groups_output:
                    self.log(f"User {username} is already in the {group} group, but still cannot access {port}")
                    self.log("Try unplugging and reconnecting the device, or reboot to apply group changes")
                    return False
    
                # Ask if user wants to add themselves to the group
                if messagebox.askyesno("Permission Error", 
                                     f"Cannot access {port}. Would you like to add your user to the '{group}' group?\n"
                                     f"This requires sudo access and you may need to log out and log back in."):
                    try:
                        # Use pkexec for GUI sudo prompt if available, otherwise use sudo
                        if shutil.which("pkexec"):
                            cmd = f"pkexec usermod -a -G {group} {username}"
                        else:
                            cmd = f"sudo usermod -a -G {group} {username}"
    
                        subprocess.run(cmd, shell=True, check=True)
                        messagebox.showinfo("Group Added", 
                                          f"Added user {username} to the {group} group.\n"
                                          f"You need to log out and log back in for this change to take effect.")
                    except subprocess.CalledProcessError as e:
                        self.log(f"Failed to add user to group: {e}")
                        messagebox.showerror("Error", "Failed to add user to group. You may need to do this manually.")
                        self.log(f"To fix manually, run: sudo usermod -a -G {group} {username}")
                    return False
                else:
                    return False
            except Exception as e:
                self.log(f"Error checking group membership: {e}")
                return False
        return True
    
    def flash_firmware(self):
        """Flash the firmware to the ESP32"""
        port = self.port_var.get()
        
        # Check port permissions first
        if not os.path.exists(port):
            self.log(f"Error: Port {port} doesn't exist. Make sure the device is connected.")
            messagebox.showerror("Port Not Found", f"Serial port {port} not found. Please check your connection and refresh ports.")
            return False
                
        if not self.check_port_permission(port):
            self.log(f"Cannot access port {port} due to permission issues. Please fix permissions and try again.")
            return False
        
        try:
            os.environ["IDF_TOOLS_PATH"] = self.idf_tools_path
            
            # Fix permissions for Linux
            if platform.system() != "Windows":
                self.check_and_fix_esp_idf_permissions()
            
            if platform.system() == "Windows":
                export_script = os.path.join(self.idf_path, "export.bat")
                # Create a temporary batch file
                with tempfile.NamedTemporaryFile(suffix='.bat', delete=False) as temp:
                    temp_script = temp.name
                    temp.write(f'call "{export_script}"\r\ncd "{self.firmware_path}"\r\nidf.py -p {port} flash\r\n'.encode())
                
                self.execute_command(temp_script)
                os.unlink(temp_script)
            else:
                export_script = os.path.join(self.idf_path, "export.sh")
                os.chmod(export_script, 0o755)
                # Explicitly use bash to ensure 'source' command works
                self.execute_command(f'/bin/bash -c \'source "{export_script}" && cd "{self.firmware_path}" && idf.py -p {port} flash\'')
            
            self.log(f"Firmware successfully flashed to {port}")
            return True
        except Exception as e:
            self.log(f"Flash failed: {str(e)}")
            return False        
        
    def refresh_ports(self):
        """Find available serial ports and update the combobox"""
        ports = self.get_serial_ports()
        self.port_combo['values'] = ports
        if ports:
            self.port_var.set(ports[0])
    
    def get_serial_ports(self):
        """Get a list of available serial ports"""
        if platform.system() == "Windows":
            from serial.tools import list_ports
            return [port.device for port in list_ports.comports()]
        else:  # Linux
            import glob
            return glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
    
    def log(self, message):
        """Add a message to the status text widget"""
        self.status_text.insert(tk.END, message + "\n")
        self.status_text.see(tk.END)
        self.root.update_idletasks()
    
    def start_installation(self):
        """Start the installation process in a separate thread"""
        if not self.port_var.get():
            messagebox.showerror("Error", "Please select a serial port")
            return
        
        threading.Thread(target=self.run_installation, daemon=True).start()
    
    
    def download_firmware(self):
        """Download the latest stable firmware from GitHub"""
        response = requests.get("https://api.github.com/repos/ArgOsciProyect/ARG_OSCI_FIRMWARE/releases/latest")
        if response.status_code != 200:
            self.log("Failed to get latest release info. Downloading from main branch instead.")
            zip_url = "https://github.com/ArgOsciProyect/ARG_OSCI_FIRMWARE/archive/refs/heads/main.zip"
        else:
            release_data = response.json()
            zip_url = release_data.get("zipball_url")
            if not zip_url:
                zip_url = "https://github.com/ArgOsciProyect/ARG_OSCI_FIRMWARE/archive/refs/heads/main.zip"
        
        self.log(f"Downloading firmware from {zip_url}")
        response = requests.get(zip_url)
        with zipfile.ZipFile(io.BytesIO(response.content)) as zip_file:
            zip_file.extractall(self.temp_dir)
            
            # Find the extracted directory (it might have a version-specific name)
            extracted_dirs = [d for d in os.listdir(self.temp_dir) 
                             if os.path.isdir(os.path.join(self.temp_dir, d)) and 
                             (d.startswith("ArgOsciProyect-ARG_OSCI_FIRMWARE") or 
                              d.startswith("ARG_OSCI_FIRMWARE"))]
            if extracted_dirs:
                # Move the contents to the firmware directory
                extracted_dir = os.path.join(self.temp_dir, extracted_dirs[0])
                os.makedirs(self.firmware_path, exist_ok=True)
                
                # Copy all contents to the firmware path
                for item in os.listdir(extracted_dir):
                    s = os.path.join(extracted_dir, item)
                    d = os.path.join(self.firmware_path, item)
                    if os.path.isdir(s):
                        shutil.copytree(s, d)
                    else:
                        shutil.copy2(s, d)
                
                self.log(f"Firmware extracted to {self.firmware_path}")
            else:
                raise Exception("Failed to extract firmware files")
    
    def setup_esp_idf(self):
        """Download and set up ESP-IDF"""
        # Set environment variables
        os.environ["IDF_TOOLS_PATH"] = self.idf_tools_path
        
        # Create directories
        os.makedirs(self.idf_path, exist_ok=True)
        os.makedirs(self.idf_tools_path, exist_ok=True)
        
        # Download ESP-IDF
        self.log("Downloading ESP-IDF v5.3.1...")
        
        # Instead of downloading a ZIP, let's clone the git repository with submodules
        self.log("Cloning ESP-IDF repository with submodules...")
        
        # First, check if git is available
        if not shutil.which("git"):
            self.log("Git is not installed. Please install git and try again.")
            raise Exception("Git is not installed. Please install git and try again.")
        
        # Remove existing ESP-IDF directory if it exists
        if os.path.exists(self.idf_path):
            shutil.rmtree(self.idf_path)
        
        # Clone the repo with submodules
        self.log("Cloning ESP-IDF repository (this may take a while)...")
        self.execute_command(f'git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git "{self.idf_path}"')
        
        self.log("ESP-IDF repository cloned successfully with all submodules")
        
        # Fix permissions if needed
        if platform.system() != "Windows":
            self.check_and_fix_esp_idf_permissions()
        
        # Install ESP-IDF dependencies
        self.log("Installing ESP-IDF dependencies...")
        
        # Determine the install script to use based on the platform
        if platform.system() == "Windows":
            install_script = os.path.join(self.idf_path, "install.bat")
        else:
            install_script = os.path.join(self.idf_path, "install.sh")
            # Make sure the script is executable
            os.chmod(install_script, 0o755)
        
        # Run the install script
        self.execute_command(install_script)
        self.log("ESP-IDF dependencies installed")

    def configure_firmware(self):
        """Configure the firmware based on user settings"""
        globals_h_path = os.path.join(self.firmware_path, "main", "globals.h")
        
        if not os.path.exists(globals_h_path):
            # Look for globals.h in other directories
            for root, dirs, files in os.walk(self.firmware_path):
                if "globals.h" in files:
                    globals_h_path = os.path.join(root, "globals.h")
                    break
        
        if not os.path.exists(globals_h_path):
            raise Exception("Could not find globals.h in the firmware files")
        
        self.log(f"Configuring globals.h at {globals_h_path}")
        
        # Read the current content
        with open(globals_h_path, 'r') as file:
            content = file.read()
        
        # Modify the SSID and password
        content = re.sub(r'#define WIFI_SSID ".*"', f'#define WIFI_SSID "{self.ssid_var.get()}"', content)
        content = re.sub(r'#define WIFI_PASSWORD ".*"', f'#define WIFI_PASSWORD "{self.password_var.get()}"', content)
        
        # Configure ADC mode
        if self.adc_mode_var.get() == "external":
            # Ensure USE_EXTERNAL_ADC is uncommented
            if "// #define USE_EXTERNAL_ADC" in content:
                content = content.replace("// #define USE_EXTERNAL_ADC", "#define USE_EXTERNAL_ADC")
            elif "#define USE_EXTERNAL_ADC" not in content:
                # Add it after the includes
                include_section_end = content.find("#endif", content.find("#ifndef GLOBALS_H"))
                content = content[:include_section_end] + "\n#define USE_EXTERNAL_ADC\n" + content[include_section_end:]
        else:
            # Comment out USE_EXTERNAL_ADC if it exists
            content = re.sub(r'#define USE_EXTERNAL_ADC', '// #define USE_EXTERNAL_ADC', content)
        
        # Write the modified content back
        with open(globals_h_path, 'w') as file:
            file.write(content)
        
        self.log("Firmware configuration updated")
    
    def execute_command(self, command, shell=True):
        """Execute a command and log the output"""
        process = subprocess.Popen(
            command,
            shell=shell,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )
        
        for line in iter(process.stdout.readline, ''):
            if line:
                self.log(line.strip())
        
        process.stdout.close()
        return_code = process.wait()
        
        if return_code != 0:
            raise Exception(f"Command failed with return code {return_code}")
    
    def __del__(self):
        """Clean up temporary files when the object is destroyed"""
        try:
            if hasattr(self, 'temp_dir') and os.path.exists(self.temp_dir):
                shutil.rmtree(self.temp_dir)
        except Exception as e:
            print(f"Error cleaning up temporary files: {str(e)}")


    def configure_and_flash(self):
        """Configure and flash without downloading everything again"""
        if not self.port_var.get():
            messagebox.showerror("Error", "Please select a serial port")
            return
        
        # Check if firmware directory exists
        if not os.path.exists(self.firmware_path) or not os.listdir(self.firmware_path):
            if messagebox.askyesno("Firmware Missing", 
                                  "Firmware files not found. Download them first?"):
                threading.Thread(target=self.run_installation, daemon=True).start()
            return
        
        # Run the configuration and flashing steps
        threading.Thread(target=self.run_configure_and_flash, daemon=True).start()
    
    def run_configure_and_flash(self):
        """Run just the configuration and flashing steps"""
        try:
            self.progress["value"] = 0
            self.log("Starting configuration and flashing...")
            
            # Update paths from UI
            self.firmware_path = self.firmware_path_var.get()
            self.idf_path = self.idf_path_var.get()
            self.idf_tools_path = self.idf_tools_path_var.get()
            
            # Check ESP-IDF
            esp_idf_exists = os.path.exists(self.idf_path) and os.path.isfile(
                os.path.join(self.idf_path, "export.sh" if platform.system() != "Windows" else "export.bat")
            )
            
            if not esp_idf_exists:
                self.log("ESP-IDF not found. Please download and install it first.")
                messagebox.showerror("ESP-IDF Missing", "ESP-IDF not found. Run the full installation first.")
                self.progress["value"] = 0
                return
            
            # Configure firmware
            self.progress["value"] = 20
            self.log("Configuring firmware...")
            self.configure_firmware()
            
            # Build firmware
            self.progress["value"] = 50
            self.log("Building firmware...")
            self.build_firmware(clean_build=False)
            
            # Flash firmware
            self.progress["value"] = 80
            self.log("Flashing firmware to ESP32...")
            if self.flash_firmware():
                self.progress["value"] = 100
                self.log("Configuration and flashing complete!")
                messagebox.showinfo("Success", "ARG_OSCI firmware has been successfully configured and flashed!")
            else:
                self.progress["value"] = 0
                self.log("Configuration and flashing failed during flashing")
                messagebox.showerror("Flash Error", "Configuration and flashing failed. Please check the logs for details.")
            
        except Exception as e:
            self.log(f"Error: {str(e)}")
            messagebox.showerror("Error", str(e))
    
    def toggle_serial_connection(self):
        """Connect or disconnect the serial monitor"""
        if self.serial_connected:
            self.disconnect_serial()
        else:
            self.connect_serial()
    
    def connect_serial(self):
        """Connect to the serial port and start monitoring"""
        import serial
        
        port = self.monitor_port_var.get()
        
        # Use a default baud rate since baud_var is not defined in the UI setup
        baud = 115200  # Standard baud rate for ESP32
        
        if not port:
            messagebox.showerror("Error", "Please select a serial port")
            return
        
        try:
            self.serial_port = serial.Serial(port, baud, timeout=0.1)
            self.serial_connected = True
            self.connect_button.config(text="Disconnect")
            self.monitor_text.insert(tk.END, f"Connected to {port} at {baud} baud\n")
            
            # Reset the stop event and start the monitoring thread
            self.stop_monitor.clear()
            self.serial_thread = threading.Thread(target=self.monitor_serial, daemon=True)
            self.serial_thread.start()
            
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))
    
    def disconnect_serial(self):
        """Disconnect from the serial port"""
        if self.serial_connected:
            self.stop_monitor.set()
            if self.serial_thread:
                self.serial_thread.join(timeout=1.0)
            
            if self.serial_port:
                self.serial_port.close()
                self.serial_port = None
            
            self.serial_connected = False
            self.connect_button.config(text="Connect")
            self.monitor_text.insert(tk.END, "Disconnected\n")
    
    def monitor_serial(self):
        """Thread function to read from serial port"""
        import serial
        
        while not self.stop_monitor.is_set() and self.serial_port:
            try:
                # Read data from serial port
                if self.serial_port.in_waiting:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    try:
                        text = data.decode('utf-8')
                        # Update UI in thread-safe manner
                        self.root.after(0, self.append_to_monitor, text)
                    except UnicodeDecodeError:
                        # If we can't decode as UTF-8, show hex values
                        hex_str = ' '.join(f'{b:02x}' for b in data)
                        self.root.after(0, self.append_to_monitor, f"<HEX: {hex_str}>\n")
                        
            except serial.SerialException:
                # Handle disconnection
                self.root.after(0, self.handle_serial_disconnect)
                break
                
            # Short sleep to prevent 100% CPU usage
            time.sleep(0.05)
    
    def append_to_monitor(self, text):
        """Append text to monitor in a thread-safe way"""
        self.monitor_text.insert(tk.END, text)
        if self.autoscroll_var.get():
            self.monitor_text.see(tk.END)
    
    def handle_serial_disconnect(self):
        """Handle unexpected serial disconnect"""
        self.disconnect_serial()
        messagebox.showwarning("Disconnected", "Serial connection was lost")
    
    def clear_monitor(self):
        """Clear the monitor text"""
        self.monitor_text.delete(1.0, tk.END)
    
    def save_monitor_log(self):
        """Save monitor contents to a file"""
        from tkinter import filedialog
        
        file_path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            title="Save Monitor Log"
        )
        
        if file_path:
            with open(file_path, 'w') as f:
                f.write(self.monitor_text.get(1.0, tk.END))
            self.monitor_text.insert(tk.END, f"\nLog saved to {file_path}\n")

    def check_and_fix_esp_idf_permissions(self):
        """Check and fix permissions on ESP-IDF tools"""
        if platform.system() != "Windows":
            self.log("Checking ESP-IDF tool permissions...")
            try:
                # Make sure idf.py has execute permission
                idf_py_path = os.path.join(self.idf_path, "tools", "idf.py")
                if os.path.exists(idf_py_path):
                    os.chmod(idf_py_path, 0o755)
                
                # Make all scripts in the tools directory executable
                tools_dir = os.path.join(self.idf_path, "tools")
                if os.path.exists(tools_dir):
                    for root, dirs, files in os.walk(tools_dir):
                        for file in files:
                            if file.endswith(".py") or file.endswith(".sh"):
                                file_path = os.path.join(root, file)
                                os.chmod(file_path, 0o755)
                
                # Fix permissions on key scripts
                for script in ["export.sh", "install.sh"]:
                    script_path = os.path.join(self.idf_path, script)
                    if os.path.exists(script_path):
                        os.chmod(script_path, 0o755)
                
                self.log("ESP-IDF permissions fixed")
                
            except Exception as e:
                self.log(f"Warning: Could not set permissions: {str(e)}")
                return False
                
            return True
        
def main():
    try:
        root = tk.Tk()
        app = ARG_OSCI_Installer(root)
        root.mainloop()
    except ImportError as e:
        print(f"Error al cargar módulos: {e}")
        print("Intentando instalar dependencias faltantes...")
        ensure_python_dependencies()
        # Si llegamos aquí, algo falló en la instalación de dependencias
        sys.exit(1)
    except Exception as e:
        print(f"Error inesperado: {e}")
        import traceback
        traceback.print_exc()
        try:
            input("\nPresione Enter para salir...")
        except:
            pass
        sys.exit(1)

if __name__ == "__main__":
    main()