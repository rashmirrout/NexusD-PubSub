/**
 * @file main.cpp
 * @brief NexusD daemon entry point
 * 
 * This is the thin executable that wires together all the library components:
 * - Discovery agent for UDP multicast peer discovery
 * - Mesh service for inter-node communication
 * - Sidecar service for application pub/sub API
 */

#include <nexusd/daemon/config.hpp>
#include <nexusd/utils/logger.hpp>
#include <nexusd/utils/uuid.hpp>
#include <nexusd/core/peer_registry.hpp>
#include <nexusd/core/discovery_agent.hpp>
#include <nexusd/services/mesh_service.hpp>
#include <nexusd/services/sidecar_service.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include <csignal>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>

using namespace nexusd;
using namespace nexusd::daemon;

// Global shutdown flag
static std::atomic<bool> g_shutdown{false};

// Signal handler
void signalHandler(int signal) {
    LOG_INFO("Daemon", "Received signal " << signal << ", initiating shutdown...");
    g_shutdown.store(true);
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    Config config = parseArgs(argc, argv);

    if (config.help) {
        printUsage(argv[0]);
        return 0;
    }

    // Configure logging
    utils::Logger::instance().setLevel(
        static_cast<utils::LogLevel>(parseLogLevel(config.log_level))
    );

    // Generate instance UUID
    const std::string instance_uuid = utils::generateUUID();

    LOG_INFO("Daemon", "NexusD starting...");
    LOG_INFO("Daemon", "Instance UUID: " << instance_uuid);
    LOG_INFO("Daemon", "Cluster: " << config.cluster_id);
    LOG_INFO("Daemon", "Discovery: " << config.mcast_addr << ":" << config.mcast_port);
    LOG_INFO("Daemon", "Mesh port: " << config.mesh_port);
    LOG_INFO("Daemon", "App port: " << config.app_port);

    // Install signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // Create shared peer registry
        auto peer_registry = std::make_shared<core::PeerRegistry>();

        // Create discovery agent
        core::DiscoveryAgent discovery_agent(
            peer_registry,
            instance_uuid,
            config.cluster_id,
            config.mcast_addr,
            config.mcast_port,
            config.mesh_port
        );

        // Start discovery agent
        discovery_agent.start();
        LOG_INFO("Daemon", "Discovery agent started");

        // Create mesh service
        auto mesh_service = std::make_unique<services::MeshServiceImpl>(peer_registry);

        // Create sidecar service
        auto sidecar_service = std::make_unique<services::SidecarServiceImpl>(
            instance_uuid,
            peer_registry
        );

        // Build and start mesh gRPC server
        std::string mesh_addr = config.bind_addr + ":" + std::to_string(config.mesh_port);
        grpc::ServerBuilder mesh_builder;
        mesh_builder.AddListeningPort(mesh_addr, grpc::InsecureServerCredentials());
        mesh_builder.RegisterService(mesh_service.get());
        auto mesh_server = mesh_builder.BuildAndStart();

        if (!mesh_server) {
            LOG_FATAL("Daemon", "Failed to start mesh server on " << mesh_addr);
            return 1;
        }
        LOG_INFO("Daemon", "Mesh server listening on " << mesh_addr);

        // Build and start sidecar gRPC server
        std::string app_addr = config.bind_addr + ":" + std::to_string(config.app_port);
        grpc::ServerBuilder app_builder;
        app_builder.AddListeningPort(app_addr, grpc::InsecureServerCredentials());
        app_builder.RegisterService(sidecar_service.get());
        auto app_server = app_builder.BuildAndStart();

        if (!app_server) {
            LOG_FATAL("Daemon", "Failed to start sidecar server on " << app_addr);
            mesh_server->Shutdown();
            return 1;
        }
        LOG_INFO("Daemon", "Sidecar server listening on " << app_addr);
        LOG_INFO("Daemon", "NexusD is ready");

        // Main loop - wait for shutdown signal
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Graceful shutdown
        LOG_INFO("Daemon", "Shutting down...");

        // Stop gRPC servers
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
        app_server->Shutdown(deadline);
        mesh_server->Shutdown(deadline);

        // Stop discovery agent
        discovery_agent.stop();

        LOG_INFO("Daemon", "NexusD stopped");
        return 0;

    } catch (const std::exception& e) {
        LOG_FATAL("Daemon", "Fatal error: " << e.what());
        return 1;
    }
}
