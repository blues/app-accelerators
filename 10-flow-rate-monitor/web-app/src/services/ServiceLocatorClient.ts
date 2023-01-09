import { UrlManager } from "../components/presentation/UrlManager";
import { NextJsUrlManager } from "../adapters/nextjs/NextJsUrlManager";

class ServiceLocatorClient {
  private urlManager?: UrlManager;

  getUrlManager(): UrlManager {
    if (!this.urlManager) {
      this.urlManager = NextJsUrlManager;
    }
    return this.urlManager;
  }
}

let Services: ServiceLocatorClient | null = null;

function services() {
  // Don’t create a ServiceLocator until it’s needed. This prevents all service
  // initialization steps from happening as soon as you import this module.
  if (!Services) {
    Services = new ServiceLocatorClient();
  }
  return Services;
}

// eslint-disable-next-line import/prefer-default-export
export { services };
