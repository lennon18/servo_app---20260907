const vscode = require("vscode");

const EXPRESSIONS = [
    ["gyro_x_dps", "gsa200_gyro_x_dps"],
    ["gyro_y_dps", "gsa200_gyro_y_dps"],
    ["gyro_z_dps", "gsa200_gyro_z_dps"],
    ["accel_x_g", "gsa200_accel_x_g"],
    ["accel_y_g", "gsa200_accel_y_g"],
    ["accel_z_g", "gsa200_accel_z_g"],
    ["temperature_c", "gsa200_temperature_c"],
    ["sample_counter", "gsa200_sample_counter"],
    ["valid_frames", "gsa200_valid_frames"],
    ["checksum_errors", "gsa200_checksum_errors"],
    ["format_errors", "gsa200_format_errors"],
    ["sequence_errors", "gsa200_sequence_errors"],
    ["dropped_frames", "gsa200_dropped_frames"],
    ["uart_errors", "gsa200_uart_errors"],
    ["valid_frames_per_sec", "gsa200_valid_frames_per_sec"],
    ["checksum_errors_per_sec", "gsa200_checksum_errors_per_sec"],
    ["format_errors_per_sec", "gsa200_format_errors_per_sec"],
    ["sequence_errors_per_sec", "gsa200_sequence_errors_per_sec"],
    ["dropped_frames_per_sec", "gsa200_dropped_frames_per_sec"],
    ["uart_errors_per_sec", "gsa200_uart_errors_per_sec"],
    ["active_baudrate", "gsa200_active_baudrate"],
    ["data_valid", "gsa200_data_valid"],
    ["speed_reference_rpm", "pcu_speed_reference_rpm"],
    ["speed_feedback_rpm", "pcu_speed_feedback_rpm"],
    ["speed_error_rpm", "pcu_speed_error_rpm"],
    ["speed_current_a", "pcu_speed_current_a"],
    ["speed_output_norm", "pcu_speed_output_norm"],
    ["speed_current_raw", "pcu_speed_current_raw"],
    ["speed_control_updates", "pcu_speed_control_updates"],
    ["speed_feedback_timeouts", "pcu_speed_feedback_timeouts"],
    ["speed_command_errors", "pcu_speed_command_errors"]
];

async function seedLiveWatch() {
    const folder = vscode.workspace.workspaceFolders?.[0];
    if (!folder) {
        return;
    }

    const cortexExtension = vscode.extensions.getExtension("marus25.cortex-debug");
    if (!cortexExtension) {
        console.error("[LiveWatchSeeder] Cortex-Debug is not installed.");
        return;
    }

    const cortex = await cortexExtension.activate();
    const provider = cortex?.liveWatchProvider;
    const root = provider?.variables;
    if (!provider || !root) {
        console.error("[LiveWatchSeeder] Cortex-Debug Live Watch provider is unavailable.");
        return;
    }

    for (const [name, expression] of EXPRESSIONS) {
        const existing = root.children?.find((node) => node.expr === expression);
        if (existing) {
            existing.name = name;
        } else {
            root.addChild(name, expression);
        }
    }

    provider.saveState();
    provider.fire();
    console.log(`[LiveWatchSeeder] Ready: ${EXPRESSIONS.length} expressions.`);
}

async function activate(context) {
    context.subscriptions.push(vscode.window.registerUriHandler({
        handleUri: seedLiveWatch
    }));
    await seedLiveWatch();
}

function deactivate() {}

module.exports = { activate, deactivate };
