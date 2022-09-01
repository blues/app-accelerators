/* eslint-disable max-classes-per-file */
import { PrismaClient } from "@prisma/client";
import AxiosHttpNotehubAccessor from "./notehub/AxiosHttpNotehubAccessor";
import AppService, { AppServiceInterface } from "./AppService";
import Config from "../../Config";
import { UrlManager } from "../components/presentation/UrlManager";
import { NextJsUrlManager } from "../adapters/nextjs/NextJsUrlManager";
import { NotehubAccessor } from "./notehub/NotehubAccessor";
import { DataProvider } from "./DataProvider";
import PrismaDatastoreEventHandler from "./prisma-datastore/PrismaDatastoreEventHandler";
import { AppEventHandler } from "./AppEvent";
import NoopAppEventHandler from "./NoopAppEventHandler";
import { PrismaDataProvider } from "./prisma-datastore/PrismaDataProvider";
// eslint-disable-next-line import/no-named-as-default
import { getPrismaClient } from "./prisma-datastore/prisma-util";
import { serverLogInfo } from "../pages/api/log";
import { NotificationsStore, TransientNotificationStore } from "./NotificationsStore";
import { CompositeEventHandler } from "./CompositeEventHandler";
import { NotificationEventHandler } from "./NotificationEventHandler";
import PrismaNotificationsStore from "./prisma-datastore/PrismaNotificationsStore";
import IDBuilder, { SimpleIDBuilder } from "./IDBuilder";
import NotehubDataProvider from "./notehub/NotehubDataProvider";
import CompositeDataProvider from "./prisma-datastore/CompositeDataProvider";
import { AttributeStore } from "./AttributeStore";
import NotehubAttributeStore from "./notehub/NotehubAttributeStore";
import PrismaAttributeStore from "./prisma-datastore/PrismaAttributeStore";
import CompositeAttributeStore from "./prisma-datastore/CompositeAttributeStore";

// ServiceLocator is the top-level consturction and dependency injection tool
// for server-side node code.
class ServiceLocatorServer {
  private appService?: AppServiceInterface;

  private urlManager?: UrlManager;

  private dataProvider?: DataProvider;

  private attributeStore?: AttributeStore;

  private notehubAccessor?: NotehubAccessor;

  private prisma?: PrismaClient;

  private eventHandler?: AppEventHandler;

  private prismaDataProvider?: PrismaDataProvider;

  private notificationsStore?: NotificationsStore;

  constructor() {
    const { notehubProvider } = Config;
    const { databaseURL } = Config;
    this.prisma = !notehubProvider ? getPrismaClient(databaseURL) : undefined;
    const message = this.prisma
      ? `Connecting to database at ${databaseURL}`
      : "Using Notehub provider";
    serverLogInfo(message);
  }

  getAppService(): AppServiceInterface {
    if (!this.appService) {
      this.appService = new AppService(
        Config.hubProjectUID,
        new SimpleIDBuilder(),
        this.getDataProvider(),
        this.getEventHandler(),
        this.getAttributeStore(),
        this.getNotificationsStore()
      );
    }
    return this.appService;
  }

  private getPrismaDataProvider(): PrismaDataProvider {
    if (!this.prisma) {
      throw new Error("Prisma is not enabled in the current deployment.");
    }
    if (!this.prismaDataProvider) {
      const projectID = IDBuilder.buildProjectID(Config.hubProjectUID);
      this.prismaDataProvider = new PrismaDataProvider(this.prisma, projectID);
    }
    return this.prismaDataProvider;
  }

  private getDataProvider(): DataProvider {
    if (!this.dataProvider) {
      const projectID = IDBuilder.buildProjectID(Config.hubProjectUID);
      const notehubProvider = new NotehubDataProvider(
        this.getNotehubAccessor(),
        projectID
      );
      if (this.prisma) {
        const dataStoreProvider = this.getPrismaDataProvider();
        const combinedProvider = new CompositeDataProvider(
          this.getEventHandler(),
          this.getNotehubAccessor(),
          notehubProvider,
          dataStoreProvider
        );
        // this is needed because the combinedProvider has a sideeffect of maintaining the
        // event handler and notehub accessor. These should be brought into the PrismaDataProvider
        this.dataProvider = combinedProvider;
      } else {
        this.dataProvider = notehubProvider;
      }
    }
    return this.dataProvider;
  }

  private getEventHandler(): AppEventHandler {
    if (!this.eventHandler) {
      this.eventHandler = this.prisma
        ? new CompositeEventHandler([
            new NotificationEventHandler(this.getNotificationsStore()),
            new PrismaDatastoreEventHandler(this.prisma),
          ])
        : new NoopAppEventHandler();
    }
    return this.eventHandler;
  }

  private getNotehubAccessor(): NotehubAccessor {
    if (!this.notehubAccessor) {
      this.notehubAccessor = new AxiosHttpNotehubAccessor(
        Config.hubBaseURL,
        Config.hubProjectUID,
        Config.hubAuthToken
      );
    }
    return this.notehubAccessor;
  }

  getAttributeStore(): AttributeStore {
    if (!this.attributeStore) {
      const notehubStore: AttributeStore = new NotehubAttributeStore(
        this.getNotehubAccessor()
      );

      if (this.prisma) {
        const prismaStore = new PrismaAttributeStore(
          this.prisma,
          this.getPrismaDataProvider()
        );
        const compositeStore = new CompositeAttributeStore([
          notehubStore,
          prismaStore,
        ]);
        compositeStore.updateDevicePin =
          prismaStore.updateDevicePin.bind(prismaStore);
        this.attributeStore = compositeStore;
      } else {
        this.attributeStore = notehubStore;
      }
    }
    return this.attributeStore;
  }

  getUrlManager(): UrlManager {
    if (!this.urlManager) {
      this.urlManager = NextJsUrlManager;
    }
    return this.urlManager;
  }

  getNotificationsStore(): NotificationsStore {
    if (!this.notificationsStore) {
      this.notificationsStore = this.prisma ? new PrismaNotificationsStore(this.prisma) : new TransientNotificationStore();
    }
    return this.notificationsStore;
  }
}

let Services: ServiceLocatorServer | null = null;

function services() {
  // Don’t create a ServiceLocator until it’s needed. This prevents all service
  // initialization steps from happening as soon as you import this module.
  if (!Services) {
    Services = new ServiceLocatorServer();
  }
  return Services;
}

// eslint-disable-next-line import/prefer-default-export
export { services };
