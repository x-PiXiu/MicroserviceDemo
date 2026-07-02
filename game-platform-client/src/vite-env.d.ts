interface ImportMetaEnv {
  readonly VITE_API_BASE_URL: string
  readonly VITE_WS_BASE_URL: string
  readonly VITE_LOG_LEVEL?: string
  /** 是否使用直连模式：true=直连IP:端口，false=通过Nginx代理（默认） */
  readonly VITE_USE_DIRECT_CONNECTION?: string
  /** 直连模式下服务 host 地址 */
  readonly VITE_SERVICE_HOST?: string
}

interface ImportMeta {
  readonly env: ImportMetaEnv
}
