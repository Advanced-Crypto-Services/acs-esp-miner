import { createContext } from "preact";
import { useState, useEffect, useContext } from "preact/hooks";
import { ComponentChildren } from "preact";

interface ThemeData {
  themeName: string;
  primaryColor: string;
  secondaryColor: string;
  backgroundColor: string;
  textColor: string;
  borderColor: string;
}

interface ThemeContextType {
  themeData: ThemeData | null;
  loading: boolean;
  error: string | null;
  fetchTheme: () => Promise<void>;
  fetchThemes: () => Promise<void>;
  applyTheme: (themeName: string) => Promise<void>;
}

const defaultThemeContext: ThemeContextType = {
  themeData: null,
  loading: false,
  error: null,
  fetchTheme: async () => {},
  fetchThemes: async () => {},
  applyTheme: async () => {},
};

export const ThemeContext = createContext<ThemeContextType>(defaultThemeContext);

export function ThemeProvider({ children }: { children: ComponentChildren }) {
  const [themeData, setThemeData] = useState<ThemeData | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // FETCH CURRENT THEME
  const fetchTheme = async () => {
    try {
      setLoading(true);
      setError(null);

      const response = await fetch("/api/themes/current");

      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }

      const data = await response.json();
      setThemeData(data);

      // Apply theme colors to CSS variables
      if (data) {
        document.documentElement.style.setProperty("--primary-color", data.primaryColor);
        document.documentElement.style.setProperty("--secondary-color", data.secondaryColor);
        document.documentElement.style.setProperty("--background-color", data.backgroundColor);
        document.documentElement.style.setProperty("--text-color", data.textColor);
        document.documentElement.style.setProperty("--border-color", data.borderColor);
        document.documentElement.style.setProperty("--primary-color-text", "#ffffff");
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to fetch theme data");
    } finally {
      setLoading(false);
    }
  };
  // LIST THEMES
  const fetchThemes = async () => {
    try {
      setLoading(true);
      setError(null);
      const response = await fetch("/api/themes");
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      // Just fetch themes - no need to return them
      await response.json();
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to fetch themes data");
    } finally {
      setLoading(false);
    }
  };
  // APPLY THEME
  const applyTheme = async (themeName: string) => {
    try {
      // Using the themeName directly in the URL
      const response = await fetch(`/api/themes/${themeName}`, {
        method: "PATCH",
      });

      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }

      // Get the response data and update themeData directly
      const data = await response.json();
      setThemeData(data);

      // Apply theme colors to CSS variables
      if (data) {
        document.documentElement.style.setProperty("--primary-color", data.primaryColor);
        document.documentElement.style.setProperty("--secondary-color", data.secondaryColor);
        document.documentElement.style.setProperty("--background-color", data.backgroundColor);
        document.documentElement.style.setProperty("--text-color", data.textColor);
        document.documentElement.style.setProperty("--border-color", data.borderColor);
        document.documentElement.style.setProperty("--primary-color-text", "#ffffff");
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to apply theme");
    }
  };

  // Fetch theme on mount
  useEffect(() => {
    fetchTheme();
  }, []);

  return (
    <ThemeContext.Provider
      value={{ themeData, loading, error, fetchTheme, fetchThemes, applyTheme }}
    >
      {children}
    </ThemeContext.Provider>
  );
}

// Custom hook to use the theme context
export function useTheme() {
  const context = useContext(ThemeContext);
  if (context === undefined) {
    throw new Error("useTheme must be used within a ThemeProvider");
  }
  return context;
}
