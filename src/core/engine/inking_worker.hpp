#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <blend2d.h>

struct PenInputSample {
    double x = 0.0;
    double y = 0.0;
    double pressure = 1.0;
    uint64_t timestampNs = 0;
};

class InkingWorker {
public:
    std::atomic<bool> isRunning{false};
    std::atomic<bool> isPenDown{false};

    // Thread synchronization
    std::thread workerThread;
    std::mutex queueMutex;
    std::mutex outputMutex;

    // Incoming high-frequency input points
    std::vector<PenInputSample> inputQueue;

    // Output geometry shared with the 120 FPS render thread
    BLPath committedPath;
    bool hasNewGeometry = false;

    // Spring-damper physical state
    double posX = 0.0, posY = 0.0;
    double velX = 0.0, velY = 0.0;
    double currentPressure = 1.0;
    bool isFirstPoint = true;

    void Start() {
        if (isRunning.load()) return;
        isRunning.store(true);
        workerThread = std::thread(&InkingWorker::WorkerLoop, this);
    }

    void Stop() {
        isRunning.store(false);
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void BeginStroke(double startX, double startY, double pressure, uint64_t timestampNs) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            inputQueue.clear();
            inputQueue.push_back({startX, startY, pressure, timestampNs});
        }
        {
            std::lock_guard<std::mutex> lock(outputMutex);
            committedPath.reset();
            committedPath.move_to(startX, startY);
            hasNewGeometry = true;
        }
        posX = startX;
        posY = startY;
        velX = 0.0;
        velY = 0.0;
        currentPressure = pressure;
        isFirstPoint = true;
        isPenDown.store(true);
    }

    void PushSample(double x, double y, double pressure, uint64_t timestampNs) {
        if (!isPenDown.load()) return;
        std::lock_guard<std::mutex> lock(queueMutex);
        inputQueue.push_back({x, y, pressure, timestampNs});
    }

    void EndStroke() {
        isPenDown.store(false);
    }

    // Called at 120 FPS by the render engine
    bool FetchLatestPath(BLPath& outPath) {
        std::lock_guard<std::mutex> lock(outputMutex);
        if (!hasNewGeometry) return false;
        outPath = committedPath;
        hasNewGeometry = false;
        return true;
    }

private:
    void WorkerLoop() {
        constexpr auto TARGET_INTERVAL = std::chrono::microseconds(2083); // ~480 Hz

        while (isRunning.load()) {
            auto loopStart = std::chrono::high_resolution_clock::now();

            if (isPenDown.load()) {
                std::vector<PenInputSample> localSamples;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    if (!inputQueue.empty()) {
                        localSamples.swap(inputQueue);
                    }
                }

                if (!localSamples.empty()) {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    
                    // Spring-damper ODE simulation parameters
                    const double kSpring = 280.0;
                    const double kDamping = 24.0;
                    const double dt = 0.002083; // 480 Hz step

                    for (const auto& sample : localSamples) {
                        if (isFirstPoint) {
                            posX = sample.x;
                            posY = sample.y;
                            isFirstPoint = false;
                            continue;
                        }

                        // Spring force: F = -k * (pos - target) - c * vel
                        double fx = -kSpring * (posX - sample.x) - kDamping * velX;
                        double fy = -kSpring * (posY - sample.y) - kDamping * velY;

                        velX += fx * dt;
                        velY += fy * dt;
                        posX += velX * dt;
                        posY += velY * dt;

                        // Add smooth curve segment to Blend2D Path
                        committedPath.line_to(posX, posY);
                    }
                    hasNewGeometry = true;
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - loopStart
            );
            if (elapsed < TARGET_INTERVAL) {
                std::this_thread::sleep_for(TARGET_INTERVAL - elapsed);
            }
        }
    }
};