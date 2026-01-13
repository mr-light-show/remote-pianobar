#!/bin/bash
# Build pianobar and run a quick exit test

set -e

echo "=== Building pianobar with exit fix ==="
make clean
make

echo ""
echo "=== Build complete! ==="
echo ""
echo "To test the exit speed fix on Ubuntu:"
echo "1. Copy the binary to Ubuntu:"
echo "   scp pianobar user@ubuntu-host:/tmp/"
echo ""
echo "2. On Ubuntu, run with debug output:"
echo "   PIANOBAR_DEBUG=8 /tmp/pianobar"
echo ""
echo "3. Play a song, then press 'q' to quit"
echo ""
echo "4. Look for these debug lines:"
echo "   BarPlayerDestroy: Stopping engine before uninit"
echo "   BarPlayerDestroy: Calling ma_engine_uninit"
echo "   BarPlayerDestroy: ma_engine_uninit completed"
echo ""
echo "Expected: Exit completes in < 2 seconds (down from 12-15 seconds)"
echo ""
echo "See TESTING_EXIT_FIX.md for detailed testing instructions"
