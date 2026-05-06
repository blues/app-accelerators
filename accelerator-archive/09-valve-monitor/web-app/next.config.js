const withAntdLess = require("next-plugin-antd-less");

/** @type {import('next').NextConfig} */
module.exports = withAntdLess({
  reactStrictMode: true,
  lessVarsFilePath: "./styles/antd-variables.less",
  // optional
  lessVarsFilePathAppendToEndOfContent: false,
  // optional https://github.com/webpack-contrib/css-loader#object
  cssLoaderOptions: {},
});
