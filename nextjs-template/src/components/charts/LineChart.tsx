import { ChartData, ChartOptions } from "chart.js";
import { CHART_DATE_FORMAT, getTooltipDisplayText } from "./chartHelper";
import BaseLineChart from "./BaseLineChart";

const LineChart = ({ label, chartColor, data }: any) => {
  const labels = data.map((obj) => obj.when);
  const values = data.map((obj) => obj.value);

  const chartData: ChartData<"line"> = {
    labels,
    datasets: [
      {
        label,
        data: values,
        backgroundColor: `${chartColor}66`,
        borderColor: `${chartColor}`,
        pointBackgroundColor: `${chartColor}`,
        pointRadius: 2,
        fill: true,
      },
    ],
  };

  const options: ChartOptions<"line"> = {
    scales: {
      xAxis: {
        grid: {
          display: false,
        },
        type: "time",
        time: {
          displayFormats: {
            millisecond: CHART_DATE_FORMAT,
            second: CHART_DATE_FORMAT,
            minute: CHART_DATE_FORMAT,
            hour: CHART_DATE_FORMAT,
            day: CHART_DATE_FORMAT,
            week: CHART_DATE_FORMAT,
            month: CHART_DATE_FORMAT,
            quarter: CHART_DATE_FORMAT,
            year: CHART_DATE_FORMAT,
          },
        },
      },
      yAxis: {
        grid: {
          drawBorder: false,
        },
        ticks: {
          stepSize: 0,
        },
      },
    },
    plugins: {
      tooltip: {
        callbacks: {
          label: (context) =>
            getTooltipDisplayText(
              context.dataset.label || "",
              context.parsed.y
            ),
        },
      },
      legend: {
        display: false,
      },
    },
  };

  return (
    <div style={{ width: "100%" }}>
      <BaseLineChart data={chartData} options={options} />
    </div>
  );
};

export default LineChart;
