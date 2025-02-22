![clock module](assets/img/synth-foundry-E01-clock-img.png)

# Synth Foundry - Clock Module

**A precision digital clock generator for modular synthesis.**  
Built from scratch, powered by the **RP2040-Zero**, and fully open-source.

🔹 **5 independent clock outputs** with precise divisions  
🔹 **Adjustable tempo (BPM) control** with hysteresis filtering  
🔹 **OLED display for real-time division feedback**  
🔹 **Hacker-friendly, fully modifiable firmware**

---

## **🎥 Watch the Full Breakdown on YouTube**

[![Watch on YouTube](https://img.youtube.com/vi/BvUXzn_yLwA/hqdefault.jpg)](https://www.youtube.com/watch?v=BvUXzn_yLwA)  

---

## **🛠 Features**

✔ **5 Clock Outputs** – Configurable time divisions based on a stable master clock.  
✔ **Intuitive BPM Control** – Adjust via encoder/switches with visual feedback.  
✔ **OLED Display** – Displays tempo and active divisions dynamically.  
✔ **RP2040-Based** – Efficient and precise timing via hardware timers.  
✔ **DIY & Mod-Friendly** – Fully open-source firmware and hardware.

---

## **📂 Repository Structure**

```
📂 clock-module
 ├── 📂 src          # Firmware source code
 ├── 📂 hardware     # KiCad schematics and PCB files
 ├── 📂 bin          # Precompiled .elf and .uf2 firmware
 ├── 📂 docs         # Technical documentation and wiring guides
 ├── 📂 assets       # Images, diagrams, and audio samples
 ├── README.md       # This file
 ├── LICENSE         # GPLv3 - Open-source and free to modify
```

---

## **🛠 Installation & Setup**

### **1️⃣ Flash the Firmware**

1. Download the latest `.uf2` firmware from the **[Releases](#)** section.
2. Hold the **BOOTSEL** button on the RP2040-Zero and connect via USB.
3. Drag and drop the `.uf2` file onto the mounted drive.

### **2️⃣ Connect the Hardware**

|Pin|Function|
|---|---|
|GPIO 0-4|Clock Outputs|
|GPIO 14-15|Switch Inputs (BPM Up/Down)|
|GPIO 26-29|ADC Inputs (Division Control)|
|GPIO 6-7|I2C for OLED Display|

### **3️⃣ Adjusting BPM & Divisions**

- Use the **switch** to tweak BPM.
- Adjust **division knobs** for real-time time subdivision changes.
- OLED display updates dynamically to show changes.

---

## **📜 License**

🔓 **GPLv3** – This module is **fully open-source**. You’re free to **modify, distribute, and sell derivatives**, as long as they remain open-source.

---

## **🤝 Contribute & Join the Community**

💬 **Discussions & Issues** → [GitHub Issues](#)  
🛠 **Submit Pull Requests** → Fork & contribute improvements!  
📡 **Follow the Synth Foundry Journey** → [YouTube](#), [Discord](#), [Instagram](#)
