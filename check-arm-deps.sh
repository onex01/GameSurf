#!/bin/bash
# check-arm-deps.sh — проверка зависимостей для кросс-компиляции

echo "=== Проверка ARM64 зависимостей ==="
for pkg in libgtk-4-dev libwebkitgtk-6.0-dev libsdl2-dev libjson-glib-dev; do
    if dpkg -l "$pkg:aarch64" &>/dev/null || [ -d "/usr/lib/aarch64-linux-gnu/pkgconfig" ]; then
        echo "✓ $pkg (вероятно установлен)"
    else
        echo "✗ $pkg — НУЖНО УСТАНОВИТЬ"
    fi
done

echo ""
echo "=== Для установки ARM64 библиотек выполни: ==="
echo "sudo dpkg --add-architecture arm64"
echo "sudo apt update"
echo "sudo apt install -y libgtk-4-dev:arm64 libwebkitgtk-6.0-dev:arm64 libsdl2-dev:arm64 libjson-glib-dev:arm64"
echo ""
echo "ИЛИ используй статическую сборку:"
echo "  ./build.sh x86_64  # только x86"
echo "  # ARM собирай на самой консоли через PortMaster"
