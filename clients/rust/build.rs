fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Compile sidecar.proto from the shared proto directory
    tonic_build::configure()
        .build_server(false)  // Client only
        .build_client(true)
        .out_dir("src/generated")
        .compile(
            &["../../proto/sidecar.proto"],
            &["../../proto"],
        )?;
    Ok(())
}
