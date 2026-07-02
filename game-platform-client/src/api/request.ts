import axios, { type AxiosInstance, type AxiosRequestConfig, type AxiosResponse } from 'axios'
import { ElMessage } from 'element-plus'
import router from '@/router'
import type { ApiResponse } from '@/types'
import { logger } from '@/utils/logger'
import { storage, STORAGE_KEYS } from '@/utils'
import { getAuthServiceUrl, getUserServiceUrl, getGameDataServiceUrl, getGomokuServerUrl, getServiceRegistryUrl } from '@/config/services'

// 为不同服务创建独立的 axios 实例
const createServiceRequest = (baseURL: string): AxiosInstance => {
  const instance = axios.create({
    baseURL,
    timeout: 30000,
    headers: {
      'Content-Type': 'application/json',
    },
  })

  // 请求拦截器
  instance.interceptors.request.use(
    (config) => {
      const token = storage.get<string>(STORAGE_KEYS.ACCESS_TOKEN)
      if (token) {
        config.headers.Authorization = `Bearer ${token}`
      }
      logger.api.debug('请求发送:', {
        url: config.url,
        method: config.method?.toUpperCase(),
        baseURL: config.baseURL,
        hasToken: !!token,
        data: config.data ? '***' : '无',
      })
      return config
    },
    (error) => {
      logger.api.error('请求配置错误:', error)
      return Promise.reject(error)
    }
  )

  // 响应拦截器
  instance.interceptors.response.use(
    (response: AxiosResponse) => {
      const responseData = response.data
      logger.api.debug('收到响应:', {
        url: response.config.url,
        status: response.status,
        hasCode: 'code' in responseData,
      })

      if (responseData && typeof responseData === 'object') {
        // 支持服务端 success 格式
        if ('success' in responseData) {
          if (responseData.success) {
            return responseData.data !== undefined ? responseData.data : responseData
          }
          const errorMsg = responseData.message || responseData.error || '请求失败'
          ElMessage.error(errorMsg)
          return Promise.reject(new Error(errorMsg))
        }
        // 支持 code 格式
        if ('code' in responseData) {
          const apiResponse = responseData as ApiResponse
          logger.api.debug('API响应(有code):', {
            code: apiResponse.code,
            message: apiResponse.message,
          })

          if (apiResponse.code === 200 || apiResponse.code === 0) {
            logger.api.info('API请求成功:', response.config.url)
            return apiResponse.data
          }

          logger.api.error('API请求失败:', {
            code: apiResponse.code,
            message: apiResponse.message,
          })
          ElMessage.error(apiResponse.message || '请求失败')
          return Promise.reject(new Error(apiResponse.message || '请求失败'))
        }
      }

      logger.api.info('API响应成功(无code):', response.config.url)
      return responseData
    },
    async (error) => {
      const originalRequest = error.config as AxiosRequestConfig & { _retry?: boolean }

      logger.api.error('请求错误:', {
        url: originalRequest?.url,
        status: error.response?.status,
        message: error.message,
      })

      if (error.response?.status === 401 && !originalRequest._retry) {
        logger.api.info('401错误，尝试刷新token')
        originalRequest._retry = true

        try {
          const refreshToken = storage.get<string>(STORAGE_KEYS.REFRESH_TOKEN)
          if (!refreshToken) {
            throw new Error('No refresh token')
          }

          logger.api.debug('发送刷新token请求')
          // 刷新 token 使用 auth service（始终使用完整服务 URL）
          const response = await axios.post(`${getAuthServiceUrl()}/refresh`, {
            refresh_token: refreshToken,
          })

          const { access_token } = response.data
          storage.set(STORAGE_KEYS.ACCESS_TOKEN, access_token)
          logger.api.info('token刷新成功')

          if (originalRequest.headers) {
            originalRequest.headers.Authorization = `Bearer ${access_token}`
          }

          return instance(originalRequest)
        } catch (refreshError) {
          logger.api.error('token刷新失败:', refreshError)
          storage.remove(STORAGE_KEYS.ACCESS_TOKEN)
          storage.remove(STORAGE_KEYS.REFRESH_TOKEN)
          router.push('/login')
          ElMessage.error('登录已过期，请重新登录')
          return Promise.reject(refreshError)
        }
      }

      const responseData = error.response?.data
      const errorMessage = responseData?.message || responseData?.error || error.message || '网络错误'
      logger.api.error('最终错误信息:', errorMessage)
      ElMessage.error(errorMessage)
      return Promise.reject(new Error(errorMessage))
    }
  )

  return instance
}

// 默认使用 auth service 的 request 实例
// services.ts 已根据连接模式（直连/代理）提供了完整 URL（含路径前缀）
const request = createServiceRequest(getAuthServiceUrl())

export default request

// 为不同服务提供独立的请求实例
// 始终使用 services.ts 的 URL，不再区分模式
export const authRequest = createServiceRequest(getAuthServiceUrl())
export const userRequest = createServiceRequest(getUserServiceUrl())
export const gameDataRequest = createServiceRequest(getGameDataServiceUrl())
export const gomokuRequest = createServiceRequest(getGomokuServerUrl())
export const serviceRequest = createServiceRequest(getServiceRegistryUrl())

// 便捷的 http 方法（向后兼容）
export const http = {
  get: <T = any>(url: string, config?: AxiosRequestConfig) =>
    request.get<T, T>(url, config) as Promise<T>,
  post: <T = any>(url: string, data?: any, config?: AxiosRequestConfig) =>
    request.post<T, T>(url, data, config) as Promise<T>,
  put: <T = any>(url: string, data?: any, config?: AxiosRequestConfig) =>
    request.put<T, T>(url, data, config) as Promise<T>,
  patch: <T = any>(url: string, data?: any, config?: AxiosRequestConfig) =>
    request.patch<T, T>(url, data, config) as Promise<T>,
  delete: <T = any>(url: string, config?: AxiosRequestConfig) =>
    request.delete<T, T>(url, config) as Promise<T>,
}
