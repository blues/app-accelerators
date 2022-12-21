import { Spin } from "antd";

const LoadingSpinner = ({
  isLoading,
  children,
}: {
  isLoading: boolean;
  children: React.ReactNode;
}): JSX.Element => {
  const msDelay = 50;
  return (
    <Spin size="large" spinning={isLoading} delay={msDelay}>
      {children}
    </Spin>
  );
};

export default LoadingSpinner;
