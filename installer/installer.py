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

        # Detect if running from a temporary filesystem (AppImage)
        script_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
        user_home = os.path.expanduser("~")
        self.is_appimage = False

        # Check if we're running from AppImage
        if "/tmp/.mount_" in script_dir or "/tmp/appimage" in script_dir:
            self.is_appimage = True
            self.log(f"Running from AppImage detected: {script_dir}")
            # When running from AppImage, always use HOME directory
            self.base_dir = os.path.join(user_home, "ARG_OSCI")
        else:
            # Not AppImage, try to use script directory if writable
            self.base_dir = os.path.join(user_home, "ARG_OSCI")  # Default
            try:
                test_file = os.path.join(script_dir, ".write_test")
                with open(test_file, 'w') as f:
                    f.write("test")
                os.remove(test_file)
                # If we get here, script_dir is writable
                self.base_dir = script_dir
            except (IOError, OSError):
                # script_dir is not writable, use home directory
                self.log(f"Script directory not writable, using {self.base_dir}")

        # Create directories if they don't exist
        try:
            os.makedirs(self.base_dir, exist_ok=True)
        except Exception as e:
            self.log(f"Warning: Could not create base directory: {e}")
            # Emergency fallback if we can't write to chosen location
            self.base_dir = os.path.join(tempfile.gettempdir(), "ARG_OSCI")
            self.log(f"Using temporary directory as fallback: {self.base_dir}")
            os.makedirs(self.base_dir, exist_ok=True)

        # Initialize default paths
        self.firmware_path = os.path.join(self.base_dir, "firmware")
        self.idf_path = os.path.join(self.base_dir, "esp-idf")
        self.idf_tools_path = os.path.join(self.base_dir, "esp-idf-tools")

        # Create directories if they don't exist
        os.makedirs(self.firmware_path, exist_ok=True)
        os.makedirs(self.idf_path, exist_ok=True)
        os.makedirs(self.idf_tools_path, exist_ok=True)

        # Setup UI before loading configuration
        self.setup_ui()

        # After UI is set up, load configuration
        config = self.load_config()
        
        # Update UI with loaded config values
        self.ssid_var.set(config["ssid"])
        self.password_var.set(config["password"])
        self.adc_mode_var.set(config["adc_mode"])
        self.firmware_path_var.set(config["firmware_path"])
        self.idf_path_var.set(config["idf_path"])
        self.idf_tools_path_var.set(config["idf_tools_path"])
        
        # Set port if it exists in config and is available
        if config["port"]:
            available_ports = self.get_serial_ports()
            if config["port"] in available_ports:
                self.port_var.set(config["port"])
        
        # Setup serial monitor
        self.serial_connected = False
        self.serial_thread = None
        self.serial_port = None
        self.stop_monitor = threading.Event()
    
    def __del__(self):
        """Clean up resources when the object is destroyed"""
        try:
            # Just close any open serial connections
            if hasattr(self, 'serial_port') and self.serial_port:
                self.serial_port.close()
        except Exception as e:
            print(f"Error cleaning up resources: {str(e)}")

    def log(self, message):
        """Add a message to the status text widget"""
        try:
            # Check if the status_text widget exists and is ready
            if hasattr(self, 'status_text') and self.status_text.winfo_exists():
                self.status_text.insert(tk.END, message + "\n")
                self.status_text.see(tk.END)
                # Update UI
                if hasattr(self, 'root') and self.root.winfo_exists():
                    self.root.update_idletasks()
        except Exception:
            # Fallback to print if UI not ready or any error occurs
            print(message)

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
        # Use self.base_dir instead of self.temp_dir here
        self.firmware_path_var = tk.StringVar(value=os.path.join(self.base_dir, "firmware"))
        ttk.Entry(path_frame, textvariable=self.firmware_path_var, width=40).grid(row=1, column=1, sticky="w")
        ttk.Button(path_frame, text="Browse", command=lambda: self.select_directory("firmware")).grid(row=1, column=2, padx=5)

        ttk.Label(path_frame, text="ESP-IDF Path:").grid(row=2, column=0, sticky="w", padx=20)
        # Use self.base_dir instead of self.temp_dir here
        self.idf_path_var = tk.StringVar(value=os.path.join(self.base_dir, "esp-idf"))
        ttk.Entry(path_frame, textvariable=self.idf_path_var, width=40).grid(row=2, column=1, sticky="w")
        ttk.Button(path_frame, text="Browse", command=lambda: self.select_directory("esp-idf")).grid(row=2, column=2, padx=5)

        ttk.Label(path_frame, text="ESP-IDF Tools Path:").grid(row=3, column=0, sticky="w", padx=20)
        # Use self.base_dir instead of self.temp_dir here
        self.idf_tools_path_var = tk.StringVar(value=os.path.join(self.base_dir, "esp-idf-tools"))
        ttk.Entry(path_frame, textvariable=self.idf_tools_path_var, width=40).grid(row=3, column=1, sticky="w")
        ttk.Button(path_frame, text="Browse", command=lambda: self.select_directory("tools")).grid(row=3, column=2, padx=5)

        ttk.Button(path_frame, text="Already have ESP-IDF installed?", command=self.find_existing_esp_idf).grid(row=4, column=1, pady=10)

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
        # Add save functionality when closing the app
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

        # Add trace to path variables to save when changed
        self.firmware_path_var.trace_add("write", self.path_changed)
        self.idf_path_var.trace_add("write", self.path_changed)
        self.idf_tools_path_var.trace_add("write", self.path_changed)
    

    def path_changed(self, *args):
        """Called when path variables are changed"""
        # Save configuration when paths are changed
        self.save_config()
    
    def on_closing(self):
        """Handle window closing event"""
        # Save current configuration
        self.save_config()
        
        # Clean up resources
        if hasattr(self, 'serial_port') and self.serial_port:
            self.serial_port.close()
        
        # Destroy the window
        self.root.destroy()

    def save_config(self):
        """Save current configuration to a file"""
        # Use self.base_dir instead of script directory for better AppImage compatibility
        config_file = os.path.join(self.base_dir, "arg_osci_installer.cfg")
        
        try:
            with open(config_file, 'w') as f:
                f.write(f"ssid={self.ssid_var.get()}\n")
                f.write(f"password={self.password_var.get()}\n")
                f.write(f"adc_mode={self.adc_mode_var.get()}\n")
                f.write(f"firmware_path={self.firmware_path_var.get()}\n")
                f.write(f"idf_path={self.idf_path_var.get()}\n")
                f.write(f"idf_tools_path={self.idf_tools_path_var.get()}\n")
                f.write(f"port={self.port_var.get()}\n")
            
            self.log("Configuration saved to file")
        except Exception as e:
            self.log(f"Error saving configuration: {e}")
            # No need to show error dialog for config save failures
            # Just log it and continue
    
    def load_config(self):
        """Load configuration from a file"""
        # First try to load from self.base_dir
        config_file = os.path.join(self.base_dir, "arg_osci_installer.cfg")
        
        # Default configuration
        config = {
            "ssid": "ESP32_AP",
            "password": "password123",
            "adc_mode": "internal",
            "firmware_path": os.path.join(self.base_dir, "firmware"),
            "idf_path": os.path.join(self.base_dir, "esp-idf"),
            "idf_tools_path": os.path.join(self.base_dir, "esp-idf-tools"),
            "port": ""
        }
    
        # Try the config file in the new location first
        if os.path.exists(config_file):
            try:
                with open(config_file, 'r') as f:
                    saved_config = {}
                    for line in f:
                        if '=' in line:
                            key, value = line.strip().split('=', 1)
                            saved_config[key] = value
    
                    # Update default config with saved values
                    for key in config:
                        if key in saved_config:
                            config[key] = saved_config[key]
    
                self.log("Configuration loaded from file")
                return config
            except Exception as e:
                self.log(f"Error loading configuration from {config_file}: {e}")
        
        # Fall back to script directory if base_dir config doesn't exist
        script_dir_config = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "arg_osci_installer.cfg")
        if os.path.exists(script_dir_config) and script_dir_config != config_file:
            try:
                with open(script_dir_config, 'r') as f:
                    saved_config = {}
                    for line in f:
                        if '=' in line:
                            key, value = line.strip().split('=', 1)
                            saved_config[key] = value
    
                    # Update default config with saved values
                    for key in config:
                        if key in saved_config:
                            config[key] = saved_config[key]
    
                self.log(f"Configuration loaded from legacy location: {script_dir_config}")
                # Save to the new location for future use
                self.log("Migrating configuration to new location...")
                
                # Don't attempt to save here as config isn't fully initialized yet
                # Just return the loaded config
            except Exception as e:
                self.log(f"Error loading legacy configuration: {e}")
    
        return config
    
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
    
    def verify_idf_tools_path(self, idf_path):
        """Verify that the current IDF tools path is valid for the given ESP-IDF path"""
        tools_path = self.idf_tools_path_var.get()
        
        if not tools_path or not os.path.exists(tools_path):
            return False
        
        # Check if this tools path contains a Python environment
        python_env_path = os.path.join(tools_path, "python_env")
        if not os.path.exists(python_env_path):
            return False
        
        # Check for the IDF Python environment
        for env_dir in os.listdir(python_env_path) if os.path.exists(python_env_path) else []:
            if env_dir.startswith("idf") and os.path.isdir(os.path.join(python_env_path, env_dir)):
                return True
        
        return False

    def find_existing_esp_idf(self):
        """Find existing ESP-IDF installations and properly configure related paths"""
        self.log("Searching for existing ESP-IDF installations...")
        
        # First check the configured path
        configured_path = self.idf_path_var.get()
        if os.path.exists(configured_path) and os.path.isfile(
            os.path.join(configured_path, "export.sh" if platform.system() != "Windows" else "export.bat")
        ):
            # If the configured path already has ESP-IDF, verify the tools path
            if not self.verify_idf_tools_path(configured_path):
                self.log("Warning: ESP-IDF tools path appears to be incorrect")
                
                # Try to find or create a valid tools path
                valid_tools_path = self.find_valid_tools_path(configured_path)
                if valid_tools_path:
                    self.log(f"Updated ESP-IDF tools path to: {valid_tools_path}")
                    self.idf_tools_path_var.set(valid_tools_path)
                    self.save_config()
            
            self.log(f"Found ESP-IDF at the configured path: {configured_path}")
            messagebox.showinfo("ESP-IDF Found", f"ESP-IDF is already configured at:\n{configured_path}")
            return
        
        # Next check environment variables for IDF_PATH
        env_locations = []
        if os.environ.get("IDF_PATH"):
            env_locations.append(os.environ.get("IDF_PATH"))
        if os.environ.get("ESP_IDF_PATH"):
            env_locations.append(os.environ.get("ESP_IDF_PATH"))
                
        # Check environment variable for IDF_TOOLS_PATH
        tools_env = os.environ.get("IDF_TOOLS_PATH")
        tools_env_valid = tools_env and os.path.exists(tools_env)
        
        # Get user home directory for platform-specific paths
        home_dir = os.path.expanduser("~")
        username = os.path.basename(home_dir)
        
        # Standard locations for ESP-IDF
        possible_locations = [
            # Environment variable locations first
            *env_locations,
            # Linux/macOS locations
            os.path.join(home_dir, "esp", "esp-idf"),
            os.path.join(home_dir, ".espressif", "esp-idf"),
            os.path.join("/opt", "esp", "esp-idf"),
            os.path.join("/usr", "local", "esp-idf"),
            # Windows locations
            os.path.join("C:\\", "esp", "esp-idf"),
            os.path.join("C:\\", "Users", username, "esp", "esp-idf"),
            os.path.join("C:\\", "Espressif", "frameworks", "esp-idf-v5.3.1")
        ]
        
        # Check the standard locations
        for location in possible_locations:
            if location and os.path.exists(location) and os.path.isfile(
                os.path.join(location, "export.sh" if platform.system() != "Windows" else "export.bat")
            ):
                # Found an ESP-IDF installation
                self.idf_path_var.set(location)
                
                # Find valid tools path
                if tools_env_valid:
                    self.idf_tools_path_var.set(tools_env)
                else:
                    valid_tools_path = self.find_valid_tools_path(location)
                    if valid_tools_path:
                        self.idf_tools_path_var.set(valid_tools_path)
                
                # Save the configuration
                self.save_config()
                
                version_info = self.get_esp_idf_version_str(location)
                messagebox.showinfo("ESP-IDF Found", f"Found ESP-IDF {version_info} at:\n{location}")
                return
        
        # If we didn't find ESP-IDF in standard locations, perform a deeper search
        self.log("Quick detection didn't find ESP-IDF. Performing deeper search...")
        
        # Directories to search more deeply
        search_dirs = [home_dir]  # Always include home directory
        
        # Add common ESP directories based on platform
        if platform.system() == "Windows":
            search_dirs.extend([
                "C:\\esp",
                "C:\\Espressif",
                os.path.join("C:\\", "Users", username, "Documents"),
                os.path.join("C:\\", "Users", username, "Projects"),
            ])
        else:  # Linux/macOS
            search_dirs.extend([
                os.path.join(home_dir, "esp"),
                os.path.join(home_dir, "Projects"),
                os.path.join(home_dir, "Documents"),
                "/opt",
            ])
        
        # List to store all found installations
        all_installations = []
        
        # Search each directory recursively
        for directory in search_dirs:
            if os.path.exists(directory):
                self.log(f"Searching in {directory}...")
                found_installations = self.find_esp_idf_recursively(directory, max_depth=3)
                if found_installations:
                    all_installations.extend(found_installations)
        
        if all_installations:
            # Sort installations by version, prioritizing 5.3.1 or newer
            all_installations.sort(key=self.get_esp_idf_version, reverse=True)
            
            # Prefer version 5.3.1 specifically
            v531_installations = [
                path for path in all_installations 
                if self.get_esp_idf_version(path) >= (5, 3, 1)
            ]
            
            # Use 5.3.1+ if available, otherwise use the highest version
            best_match = v531_installations[0] if v531_installations else all_installations[0]
            self.idf_path_var.set(best_match)
            
            # Find a valid tools path for the selected ESP-IDF
            valid_tools_path = self.find_valid_tools_path(best_match)
            if valid_tools_path:
                self.idf_tools_path_var.set(valid_tools_path)
            
            # Save the configuration
            self.save_config()
            
            version_info = self.get_esp_idf_version_str(best_match)
            messagebox.showinfo("ESP-IDF Found", 
                              f"Found ESP-IDF {version_info} at:\n{best_match}\n\n"
                              f"Total ESP-IDF installations found: {len(all_installations)}")
            return
        
        # If still no ESP-IDF found, ask for manual location
        manual_response = messagebox.askquestion("ESP-IDF Not Found", 
                                              "Automatic detection couldn't find ESP-IDF installation.\n\n"
                                              "Would you like to select an ESP-IDF directory manually?")
        if manual_response == 'yes':
            from tkinter import filedialog
            selected_dir = filedialog.askdirectory(title="Select ESP-IDF Directory")
            
            if selected_dir:
                # Check if this is a valid ESP-IDF directory
                if os.path.isfile(os.path.join(selected_dir, "export.sh" if platform.system() != "Windows" else "export.bat")):
                    self.idf_path_var.set(selected_dir)
                    
                    # Find a valid tools path for the selected ESP-IDF
                    valid_tools_path = self.find_valid_tools_path(selected_dir)
                    if valid_tools_path:
                        self.idf_tools_path_var.set(valid_tools_path)
                        
                    # Save the configuration
                    self.save_config()
                    
                    version_info = self.get_esp_idf_version_str(selected_dir)
                    messagebox.showinfo("ESP-IDF Found", f"Selected ESP-IDF {version_info}")
                    return
                else:
                    messagebox.showerror("Invalid Directory", "The selected directory doesn't appear to be a valid ESP-IDF installation")
        
        # If no ESP-IDF found, inform the user it will be downloaded
        messagebox.showinfo("ESP-IDF Not Found", 
                          "No existing ESP-IDF installation found. It will be downloaded during installation.")        
    
    def find_valid_tools_path(self, idf_path):
        """Find a valid tools path for the given ESP-IDF path"""
        home_dir = os.path.expanduser("~")
        
        # Ordered list of potential valid tool paths
        potential_paths = [
            os.environ.get("IDF_TOOLS_PATH"),  # Check environment variable first
            os.path.join(home_dir, ".espressif"),  # Standard location
            os.path.join(os.path.dirname(idf_path), "esp-idf-tools"),  # Next to ESP-IDF
            os.path.join(home_dir, "esp", "esp-idf-tools"),  # Common alternative
        ]
        
        # Check if any of these paths exist
        for path in potential_paths:
            if path and os.path.exists(path):
                # Path exists - now check if it's a valid tools path with Python env
                python_env_path = os.path.join(path, "python_env")
                if os.path.exists(python_env_path):
                    # Found existing Python env - this is probably a valid tools path
                    return path
        
        # If no existing valid path found, use the standard .espressif location
        standard_path = os.path.join(home_dir, ".espressif")
        os.makedirs(standard_path, exist_ok=True)
        return standard_path

    def find_esp_idf_recursively(self, directory, max_depth=3, current_depth=0):
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
                    # Skip hidden directories and certain common directories that won't have ESP-IDF
                    if item.startswith('.') or item in ['node_modules', 'venv', 'env', '__pycache__']:
                        continue
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
            
            # Save current configuration
            self.save_config()
            
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
        """Download the latest firmware from GitHub main branch"""
        # Always use the main branch
        zip_url = "https://github.com/ArgOsciProyect/ARG_OSCI_FIRMWARE/archive/refs/heads/main.zip"
        
        self.log(f"Downloading firmware from {zip_url}")
        
        try:
            response = requests.get(zip_url)
            response.raise_for_status()  # Raise an exception for HTTP errors
            
            # Create a temporary directory for extraction
            with tempfile.TemporaryDirectory() as temp_extract_dir:
                with zipfile.ZipFile(io.BytesIO(response.content)) as zip_file:
                    zip_file.extractall(temp_extract_dir)
                    
                    # Find the extracted directory (should be ARG_OSCI_FIRMWARE-main)
                    extracted_dirs = [d for d in os.listdir(temp_extract_dir) 
                                   if os.path.isdir(os.path.join(temp_extract_dir, d)) and 
                                      "ARG_OSCI_FIRMWARE" in d]
                                      
                    if extracted_dirs:
                        # Move the contents to the firmware directory
                        extracted_dir = os.path.join(temp_extract_dir, extracted_dirs[0])
                        firmware_path = self.firmware_path_var.get()
                        os.makedirs(firmware_path, exist_ok=True)
                        
                        self.log(f"Extracting firmware to {firmware_path}...")
                        
                        # Copy all contents to the firmware path
                        for item in os.listdir(extracted_dir):
                            s = os.path.join(extracted_dir, item)
                            d = os.path.join(firmware_path, item)
                            if os.path.isdir(s):
                                if os.path.exists(d):
                                    shutil.rmtree(d)
                                shutil.copytree(s, d)
                            else:
                                shutil.copy2(s, d)
                        
                        self.log(f"Firmware extraction completed successfully")
                    else:
                        raise Exception("Failed to find the firmware directory after extraction")
        except requests.exceptions.RequestException as e:
            self.log(f"Error downloading firmware: {e}")
            raise Exception(f"Failed to download firmware: {e}")
        except (zipfile.BadZipFile, zipfile.LargeZipFile) as e:
            self.log(f"Error extracting zip file: {e}")
            raise Exception(f"Failed to extract firmware: {e}")
        except Exception as e:
            self.log(f"Unexpected error during firmware download: {e}")
            raise          

    def setup_esp_idf(self):
        """Download and set up ESP-IDF"""
        # Set environment variables
        os.environ["IDF_TOOLS_PATH"] = self.idf_tools_path_var.get()
        
        # Create directories
        os.makedirs(self.idf_path, exist_ok=True)
        os.makedirs(self.idf_tools_path, exist_ok=True)
        
        # Download ESP-IDF
        self.log("Downloading ESP-IDF v5.3.1...")
        
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
        
        # Install ESP-IDF dependencies with IDF_TOOLS_PATH explicitly set
        self.log("Installing ESP-IDF dependencies...")
        
        # Determine the install script to use based on the platform
        if platform.system() == "Windows":
            install_script = os.path.join(self.idf_path, "install.bat")
            install_cmd = f'set "IDF_TOOLS_PATH={self.idf_tools_path}" && "{install_script}"'
        else:
            install_script = os.path.join(self.idf_path, "install.sh")
            # Make sure the script is executable
            os.chmod(install_script, 0o755)
            install_cmd = f'export IDF_TOOLS_PATH="{self.idf_tools_path}" && "{install_script}"'
        
        # Run the install script
        self.execute_command(install_cmd)
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