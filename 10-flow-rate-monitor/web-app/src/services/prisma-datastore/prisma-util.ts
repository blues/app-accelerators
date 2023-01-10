/* eslint-disable import/prefer-default-export */
import { PrismaClient } from "@prisma/client";

declare global {
  // eslint-disable-next-line no-var, vars-on-top
  var prisma: PrismaClient;
}

// Prevent Next.js from using creating multiple Prisma connections
// during development.
// See: https://www.prisma.io/docs/support/help-articles/nextjs-prisma-client-dev-practices
export function getPrismaClient(databaseURL: string): PrismaClient {
  if (!global.prisma) {
    global.prisma = new PrismaClient({
      datasources: { db: { url: databaseURL } },
    });
  }
  return global.prisma;
}
