import * as React from "react";
import { NavChildProps } from "@/components/ui/sidebar-pill-nav";
import { FieldSection, FieldSections } from "./form-components";
import { useSettings } from "@/lib/settings";

const ConnectionStatus: React.FC = () => {
  const { about, isLoadingAbout } = useSettings();

  if (isLoadingAbout || !about) {
    return null;
  }

  const fields = [
    { label: "SSID", value: about.wifi_ssid },
    { label: "IP Address", value: about.ip_address },
    { label: "Gateway", value: about.wifi_gateway },
    { label: "Subnet", value: about.wifi_subnet },
    { label: "DNS", value: about.wifi_dns },
    { label: "MAC", value: about.wifi_mac },
    {
      label: "Signal",
      value: about.wifi_rssi != null ? `${about.wifi_rssi} dBm` : undefined,
    },
  ];

  return (
    <div>
      <h2 className="text-2xl font-bold">Connection Status</h2>
      <hr className="my-4" />
      <div className="space-y-2">
        {fields.map(
          ({ label, value }) =>
            value != null && (
              <div key={label} className="flex">
                <strong className="w-40">{label}:</strong>
                <span>{value}</span>
              </div>
            )
        )}
      </div>
    </div>
  );
};

export const NetworkSettings: React.FC<NavChildProps<"network">> = () => (
  <FieldSections>
    <ConnectionStatus />
    <FieldSection
      title="Security"
      fields={["admin_username", "admin_password"]}
      fieldTypes={{
        admin_password: "password",
      }}
    />
    <FieldSection
      title="WiFi"
      fields={[
        "wifi_ssid",
        "wifi_password",
        "wifi_ssid_secondary",
        "wifi_password_secondary",
        "hostname",
        "wifi_static_ip",
        "wifi_static_ip_gateway",
        "wifi_static_ip_netmask",
        "wifi_dns",
        "wifi_mode",
        "wifi_portal_on_fail",
      ]}
      fieldNames={{
        wifi_ssid: "SSID",
        wifi_password: "Password",
        wifi_ssid_secondary: "Fallback SSID",
        wifi_password_secondary: "Fallback Password",
        wifi_static_ip: "Static IP",
        wifi_static_ip_gateway: "Static IP Gateway",
        wifi_static_ip_netmask: "Static IP Netmask",
        wifi_dns: "DNS Server",
        wifi_portal_on_fail: "Portal on Fail",
      }}
      fieldTypes={{
        wifi_password: "password",
        wifi_password_secondary: "password",
      }}
    />
  </FieldSections>
);
