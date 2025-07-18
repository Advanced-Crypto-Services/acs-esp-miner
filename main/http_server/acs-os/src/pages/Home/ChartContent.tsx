import { useMemo, useState, useEffect } from "preact/hooks";
import RealTimeApiChart from "../../components/Chart/RealTimeApiChart";
import { getSystemInfo, SystemInfo } from "../../utils/api";
import { logger } from "../../utils/logger";
import {
  createHashrateDataFetcher,
  createTemperatureDataFetcher,
  createPowerDataFetcher,
} from "../../utils/realTimeDataFetcher";
import {
  CHART_CONFIGS,
  calculateChartMemory,
  formatMemorySize,
  validateChartConfig,
  ChartTimeConfig,
} from "../../utils/chartMemoryUtils";
import { Activity, Thermometer, Target } from "lucide-preact";

const ChartContent = () => {
  // System info state management
  const [systemInfo, setSystemInfo] = useState<SystemInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Create data fetchers for different metrics
  const hashrateDataFetcher = useMemo(() => createHashrateDataFetcher(), []);
  const temperatureDataFetcher = useMemo(() => createTemperatureDataFetcher(), []);
  const powerDataFetcher = useMemo(() => createPowerDataFetcher(), []);

  // State for selected chart configuration
  const [selectedConfigKey, setSelectedConfigKey] = useState<string>("FULL_DAY");

  const chartConfig = CHART_CONFIGS[selectedConfigKey];
  const memoryInfo = calculateChartMemory(chartConfig.dataPoints, chartConfig.intervalSeconds);
  const configValidation = validateChartConfig(chartConfig);

  // Fetch system info
  useEffect(() => {
    const fetchData = async () => {
      try {
        setLoading(true);
        const data = await getSystemInfo();
        setSystemInfo(data);
        setError(null);
      } catch (err) {
        setError("Failed to load system information");
        logger.error("Failed to load system information", err);
      } finally {
        setLoading(false);
      }
    };

    fetchData();

    // Refresh data every 10 seconds
    const intervalId = setInterval(fetchData, 10000);

    return () => clearInterval(intervalId);
  }, []);

  return (
    <div className='min-h-screen bg-gradient-to-br from-slate-50 to-gray-100'>
      {/* Header Section with Gradient */}
      <div className='bg-gradient-to-r from-blue-600 via-blue-700 to-indigo-800 text-white'>
        <div className='max-w-7xl text-left px-4 sm:px-6 py-8 sm:py-12'>
          <div className='flex flex-col xl:flex-row justify-between items-start xl:items-center gap-6 lg:gap-8'>
            <div className='flex-1'>
              <h1 className='text-3xl sm:text-4xl lg:text-5xl font-bold mb-3 sm:mb-4 leading-tight'>
                Real-Time Mining Dashboard
              </h1>
              <p className='text-sm md:text-xl text-blue-100 max-w-2xl leading-relaxed'>
                Monitor your mining performance with live data updates every 10 seconds or customize
                your duration and interval settings.
              </p>
            </div>

            {/* Performance Stats Card - Mobile Optimized */}
            <div className='w-full xl:w-auto xl:min-w-[320px] bg-white/10 backdrop-blur-sm rounded-xl p-4 sm:p-6 border border-white/20'>
              <div className='flex items-center justify-between mb-3 sm:mb-4'>
                <h3 className='text-base sm:text-lg font-semibold text-blue-100'>
                  Miner Specifications
                </h3>
              </div>

              <div className='space-y-2 text-xs sm:text-sm'>
                <div className='flex justify-between items-center'>
                  <span className='text-blue-200'>ASIC Model:</span>
                  <span className='font-medium text-right'>
                    {loading ? "Loading..." : systemInfo?.ASICModel || "N/A"}
                  </span>
                </div>
                <div className='flex justify-between items-center'>
                  <span className='text-blue-200'>Autotune Preset:</span>
                  <span className='font-medium text-right'>
                    {loading
                      ? "Loading..."
                      : systemInfo?.autotune_preset
                      ? systemInfo.autotune_preset.charAt(0).toUpperCase() +
                        systemInfo.autotune_preset.slice(1)
                      : "N/A"}
                  </span>
                </div>
                <div className='flex justify-between items-center'>
                  <span className='text-blue-200'>Expected Hashrate:</span>
                  <span className='font-medium text-right'>
                    {loading
                      ? "Loading..."
                      : systemInfo?.expectedHashrate
                      ? `${(systemInfo.expectedHashrate / 1000).toFixed(2)} TH/s`
                      : "N/A"}
                  </span>
                </div>
                <div className='flex justify-between items-center'>
                  <span className='text-blue-200'>Uptime:</span>
                  <span className='font-medium text-right'>
                    {loading
                      ? "Loading..."
                      : systemInfo?.uptimeSeconds
                      ? `${Math.floor(systemInfo.uptimeSeconds / 60)}m ${
                          systemInfo.uptimeSeconds % 60
                        }s`
                      : "N/A"}
                  </span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Main Content */}
      <div className='max-w-7xl mx-auto px-4 sm:px-6 py-6 sm:py-8'>
        {/* Mining Metrics Cards */}
        <div className='grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4 sm:gap-6 mb-6 sm:mb-8'>
          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-4 sm:p-6'>
            <div className='text-center'>
              <div className='w-8 h-8 sm:w-10 sm:h-10 bg-green-100 rounded-lg flex items-center justify-center mx-auto mb-2 sm:mb-3'>
                <Activity className='w-5 h-5 sm:w-6 sm:h-6 text-green-600' />
              </div>
              <h3 className='font-semibold text-gray-900 mb-1 text-sm sm:text-base'>Hash Rate</h3>
              <p className='text-xs sm:text-sm text-gray-500 mb-2 sm:mb-3'>
                {loading ? "Loading..." : error ? "Error" : "Current performance"}
              </p>
              <p className='text-xl sm:text-2xl font-bold text-gray-900 mb-1 sm:mb-2'>
                {loading
                  ? "--"
                  : error
                  ? "N/A"
                  : systemInfo?.hashRate
                  ? `${(systemInfo.hashRate / 1000).toFixed(2)} TH/s`
                  : "--"}
              </p>
              <p className='text-xs sm:text-sm text-gray-600 max-w-50 mx-auto leading-relaxed'>
                Real-time hashrate produced by your miner.
              </p>
            </div>
          </div>

          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-4 sm:p-6'>
            <div className='text-center'>
              <div className='w-8 h-8 sm:w-10 sm:h-10 bg-orange-100 rounded-lg flex items-center justify-center mx-auto mb-2 sm:mb-3'>
                <Thermometer className='w-5 h-5 sm:w-6 sm:h-6 text-orange-600' />
              </div>
              <h3 className='font-semibold text-gray-900 mb-1 text-sm sm:text-base'>Temperature</h3>
              <p className='text-xs sm:text-sm text-gray-500 mb-2 sm:mb-3'>
                {loading ? "Loading..." : error ? "Error" : "ASIC temperature"}
              </p>
              <p className='text-xl sm:text-2xl font-bold text-gray-900 mb-1 sm:mb-2'>
                {loading ? "--" : error ? "N/A" : systemInfo?.temp ? `${systemInfo.temp}°C` : "--"}
              </p>
              <p className='text-xs sm:text-sm text-gray-600 max-w-50 mx-auto leading-relaxed'>
                Current operating temperature of your mining ASIC chip.
              </p>
            </div>
          </div>

          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-4 sm:p-6 sm:col-span-2 lg:col-span-1'>
            <div className='text-center'>
              <div className='w-8 h-8 sm:w-10 sm:h-10 bg-purple-100 rounded-lg flex items-center justify-center mx-auto mb-2 sm:mb-3'>
                <Target className='w-5 h-5 sm:w-6 sm:h-6 text-purple-600' />
              </div>
              <h3 className='font-semibold text-gray-900 mb-1 text-sm sm:text-base'>Best Diff</h3>
              <p className='text-xs sm:text-sm text-gray-500 mb-2 sm:mb-3'>
                {loading ? "Loading..." : error ? "Error" : "Highest difficulty"}
              </p>
              <p className='text-xl sm:text-2xl font-bold text-gray-900 mb-1 sm:mb-2'>
                {loading
                  ? "--"
                  : error
                  ? "N/A"
                  : systemInfo?.bestDiff
                  ? systemInfo.bestDiff.toLocaleString()
                  : "--"}
              </p>
              <p className='text-xs sm:text-sm text-gray-600 max-w-50 mx-auto leading-relaxed'>
                The highest difficulty share found by your miner.
              </p>
            </div>
          </div>
        </div>

        {/* Performance Recommendations */}
        {configValidation.warnings.length > 0 && (
          <div className='bg-amber-50 border border-amber-200 rounded-xl p-4 sm:p-6 mb-6 sm:mb-8'>
            <div className='flex items-start gap-3'>
              <div className='w-6 h-6 sm:w-8 sm:h-8 bg-amber-100 rounded-lg flex items-center justify-center flex-shrink-0 mt-0.5'>
                <svg
                  className='w-4 h-4 sm:w-5 sm:h-5 text-amber-600'
                  fill='none'
                  stroke='currentColor'
                  viewBox='0 0 24 24'
                >
                  <path
                    strokeLinecap='round'
                    strokeLinejoin='round'
                    strokeWidth={2}
                    d='M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z'
                  />
                </svg>
              </div>
              <div className='flex-1 min-w-0'>
                <h3 className='font-semibold text-amber-800 mb-2 text-sm sm:text-base'>
                  Performance Recommendations
                </h3>
                <ul className='space-y-1 text-xs sm:text-sm text-amber-700'>
                  {configValidation.warnings.map((warning, index) => (
                    <li key={index} className='flex items-start gap-2'>
                      <span className='text-amber-500 mt-1 flex-shrink-0'>•</span>
                      <span className='break-words'>{warning}</span>
                    </li>
                  ))}
                </ul>
              </div>
            </div>
          </div>
        )}

        {/* Charts Section */}
        <div className='space-y-6 sm:space-y-8'>
          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-2 sm:p-4 overflow-hidden'>
            <RealTimeApiChart
              title='Hash Rate'
              dataFetcher={hashrateDataFetcher}
              updateInterval={chartConfig.intervalSeconds * 1000}
              maxDataPoints={chartConfig.dataPoints}
              color='#10b981'
              unit='GH/s'
              defaultUseAreaChart={true}
              defaultDataAggregation={5}
              chartConfigs={CHART_CONFIGS}
              selectedConfigKey={selectedConfigKey}
              onConfigChange={setSelectedConfigKey}
              lineStyle='dotted'
            />
          </div>
        </div>
      </div>
    </div>
  );
};

export default ChartContent;
