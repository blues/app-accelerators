import { Layout } from "antd";
import styles from "../../styles/Footer.module.scss";

const FooterComponent = () => {
  const { Footer } = Layout;
  return (
    <div className={styles.footerWrapper}>
      <hr className={styles.footerDivider} />
      <Footer className={styles.footer}>
        <div>
          Cloud-connected by{` `}
          <span>
            <a
              target="_blank"
              href="https://blues.io/products"
              rel="noreferrer"
              data-testid="notecard-link"
            >
              Notecard
            </a>
          </span>
        </div>
        <div>
          <a href="https://github.com/blues/app-accelerators/tree/main/indoor-floor-level-tracker">
            About
          </a>
        </div>
        <div>
          Developed by{` `}
          <span>
            <a
              target="_blank"
              href="https://blues.io"
              rel="noreferrer"
              data-testid="blues-link"
            >
              Blues Inc.
            </a>
          </span>
        </div>
      </Footer>
    </div>
  );
};

export default FooterComponent;
