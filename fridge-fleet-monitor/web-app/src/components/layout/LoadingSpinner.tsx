import { Spin } from "antd";

export const LoadingSpinner = ({
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

const de = { LoadingSpinner };
export default de;
