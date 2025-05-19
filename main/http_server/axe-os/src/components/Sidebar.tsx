import { ComponentChildren } from "preact";
import { useState } from "preact/hooks";
import { useTheme } from "../context/ThemeContext";

interface SidebarProps {
  children?: ComponentChildren;
}

const navItems = [
  {
    label: "Dashboard",
    href: "/",
  },
  {
    label: "Miners",
    href: "/miners",
  },
  {
    label: "Theme",
    href: "/theme",
  },
  {
    label: "Settings",
    href: "/settings",
  },
];

export function Sidebar({ children }: SidebarProps) {
  const [logoLoaded, setLogoLoaded] = useState(true);
  const { getThemeLogo } = useTheme();

  // Get the logo URL for the current theme
  const logoUrl = getThemeLogo();

  return (
    <aside class='fixed left-0 top-0 z-40 h-screen w-64 transform bg-gray-900 transition-transform sm:translate-x-0'>
      <div class='flex h-full flex-col overflow-y-auto border-r border-gray-800 px-3 py-4'>
        <div class='mb-10 flex items-center px-2'>
          <img src={logoUrl} alt='Logo' class='h-10' onLoad={() => setLogoLoaded(true)} />
        </div>

        <nav class='flex-1 space-y-2'>
          {navItems.map((item) => (
            <a
              key={item.href}
              href={item.href}
              class='flex items-center rounded-lg px-4 py-2 text-gray-300 hover:bg-gray-800'
            >
              <span>{item.label}</span>
            </a>
          ))}
        </nav>

        {/* Status indicator */}
        <div class='mt-auto pt-4 border-t border-gray-800'>
          <div class='flex items-center gap-2 px-4 py-2'>
            <div class='h-2 w-2 rounded-full bg-green-500'></div>
            <span class='text-sm text-gray-300'>Connected</span>
          </div>
        </div>
      </div>
    </aside>
  );
}
