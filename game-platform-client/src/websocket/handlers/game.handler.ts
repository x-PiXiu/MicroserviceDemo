/**
 * 游戏消息处理器
 *
 * 功能：
 * - 处理所有服务器推送的消息类型
 * - 更新对应的 Pinia Store
 * - 提供类型安全的消息处理
 *
 * 消息类型映射：
 * - welcome → handleWelcome
 * - auth_success → handleAuthSuccess
 * - ready_confirm → handleReadyConfirm
 * - player_ready → handlePlayerReady
 * - all_players_ready → handleAllPlayersReady
 * - game_state → handleGameState
 * - move_result → handleMoveResult
 * - game_end → handleGameEnd
 * - achievement_unlocked → handleAchievementUnlocked
 * - leaderboard_update → handleLeaderboardUpdate
 * - currency_update → handleCurrencyUpdate
 * - time_update → handleTimeUpdate
 * - chat → handleChat
 * - spectator_change → handleSpectatorChange
 * - notification → handleNotification
 * - error → handleError
 *
 * 设计参考：game_platform_client_optimization_plan.md 阶段2
 */
import { useGameStore } from '@/stores/game.store'
import { useGameDataStore } from '@/stores/gamedata.store'
import { useAuthStore } from '@/stores/auth.store'
import { useMatchStore } from '@/stores/match.store'
import type {
  WelcomeMessage,
  AuthSuccessMessage,
  AuthFailedMessage,
  RoomConfigMessage,
  PlayerPieceAssignedMessage,
  ReadyConfirmMessage,
  GameStateMessage,
  MoveResultMessage,
  GameEndMessage,
  TimeUpdateMessage,
  UndoRequestMessage,
  MoveUndoneMessage,
  PlayerSurrenderedMessage,
  ChatBroadcastMessage,
  SpectatorChangeMessage,
  ErrorMessage,
  ServerMessage,
  // v2.1.0 新增
  RoomListMessage,
  SubscriptionConfirmMessage,
  RoomAddedMessage,
  // RoomUpdatedMessage - 使用 any 类型处理，因为服务端返回格式与文档不同
  // RoomRemovedMessage - 使用 any 类型处理，兼容 roomId 和 room_id
  // RoomCreatedMessage - 使用 any 类型处理，因为服务端返回格式与文档不同
  // RoomJoinedMessage - 使用 any 类型处理，兼容 camelCase 和 snake_case
  RoomLeftMessage,
  // PlayerJoinedMessage 和 PlayerLeftMessage 已通过 any 类型处理
  GameStartedMessage,
  // v2.6.0 新增：游戏结算
  GameSettlementMessage,
  // v2.6.0 新增：匹配系统
  MatchStatusUpdateMessage,
  MatchFoundMessage,
  MatchTimeoutMessage,
  // 结算数据类型
  GameSettlementData,
  TierString,
  CurrencyType,
} from '@/types'

/**
 * 消息处理器回调类型
 */
export type MessageHandlerCallback<T = any> = (data: T) => void

/**
 * 游戏消息处理器集合
 */
export const gameHandlers = {
  // ==================== 认证相关 ====================

  /**
   * 处理服务器欢迎消息
   */
  handleWelcome: (message: WelcomeMessage, callback?: MessageHandlerCallback<WelcomeMessage>) => {
    console.log('[GameHandler] Welcome:', message.player_id, message.server_info)
    callback?.(message)
  },

  /**
   * 处理认证成功消息
   */
  handleAuthSuccess: (message: AuthSuccessMessage, callback?: MessageHandlerCallback<AuthSuccessMessage>) => {
    console.log('[GameHandler] Auth success:', message.data.playerId)

    // 更新认证状态
    if (message.data.authenticated) {
      console.log('[GameHandler] Player authenticated successfully')
    }

    callback?.(message)
  },

  /**
   * 处理认证失败消息
   */
  handleAuthFailed: (message: AuthFailedMessage, callback?: MessageHandlerCallback<AuthFailedMessage>) => {
    console.error('[GameHandler] Auth failed:', message.data.error, message.data.message)

    // 可以在这里添加认证失败处理逻辑
    // 例如：跳转到登录页、显示错误提示等
    const authStore = useAuthStore()
    authStore.logout()

    callback?.(message)
  },

  // ==================== 房间配置相关 ====================

  /**
   * 处理房间配置消息
   */
  handleRoomConfig: (message: RoomConfigMessage, callback?: MessageHandlerCallback<RoomConfigMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Room config:', message.data)

    // 更新房间配置
    // 注意：gameMode 在 RoomConfigMessage 中是 string，需要转换
    const gameModeMap: Record<string, number> = {
      'freestyle': 0,
      'renju': 1,
      'swap2': 2,
      'pro': 3,
      'tournament': 4,
    }
    const gameMode = typeof message.data.gameMode === 'string'
      ? (gameModeMap[message.data.gameMode] ?? 0)
      : message.data.gameMode

    gameStore.updateGameConfig({
      gameMode,
      gameModeStr: message.data.gameModeStr || message.data.gameMode,
      timeLimit: message.data.timeLimit,
      incrementPerMove: message.data.incrementPerMove,
      allowUndo: message.data.allowUndo,
      allowSpectators: message.data.allowSpectators,
      maxSpectators: message.data.maxSpectators,
      rankingEnabled: message.data.rankingEnabled ?? false,
    })

    callback?.(message)
  },

  /**
   * 处理房间信息消息
   * 包含房间内的玩家列表信息
   * 服务端 v2.2+: players[] 包含 piece, username, nickname, rating, level
   * 服务端 v2.7+: players[] 包含 ready 字段（准备状态）
   * v2.5.4: 兼容 camelCase 和 snake_case
   */
  handleRoomInfo: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    const authStore = useAuthStore()
    const data = message.data || {}

    console.log('[GameHandler] Room info received:', data)
    console.log('[GameHandler] Current currentGame before update:', gameStore.currentGame)

    // v2.5.4: 兼容 camelCase 和 snake_case
    const roomId = data.roomId || data.room_id
    const creatorId = data.creatorId || data.creator_id

    // 更新当前房间信息
    if (roomId) {
      // 检查当前用户是否是创建者
      const isCreator = creatorId === authStore.user?.id

      console.log('[GameHandler] Checking isCreator:', {
        creator_id: creatorId,
        my_id: authStore.user?.id,
        isCreator
      })

      gameStore.setCurrentRoom({
        id: roomId,
        isCreator,
        roomInfo: data,
      })

      console.log('[GameHandler] Room info - isCreator set to:', isCreator, '(creator_id:', creatorId, ', my_id:', authStore.user?.id + ')')
      console.log('[GameHandler] After setCurrentRoom, currentRoomInfo:', gameStore.currentRoomInfo)

      // 更新玩家信息到 currentGame
      if (data.players && data.players.length > 0) {
        console.log('[GameHandler] Players in room_info:', data.players)
        // 根据玩家列表更新
        let blackPlayer: any = null
        let whitePlayer: any = null
        const unassignedPlayers: any[] = []

        data.players.forEach((p: any) => {
          console.log('[GameHandler] Processing player:', p)
          // v2.5.4: 兼容 playerId 和 player_id
          const playerId = p.playerId || p.player_id
          const playerInfo = {
            id: playerId,
            username: p.username || p.nickname || playerId?.split('_')[0] || '玩家',
            rating: p.rating || 1000,
          }

          // v2.7.0: 处理准备状态（服务端新增 ready 字段）
          if (playerId && p.ready !== undefined) {
            console.log('[GameHandler] Player ready state from room_info:', playerId, p.ready)
            gameStore.updatePlayerReadyState(playerId, Boolean(p.ready))
          }

          // 优先使用服务端返回的 piece 字段（1=黑, 2=白）
          const piece = p.piece
          console.log('[GameHandler] Player piece:', playerId, piece)
          if (piece === 1 || piece === '1' || piece === 'black') {
            blackPlayer = playerInfo
            gameStore.updatePlayerPiece(playerId, 1)
          } else if (piece === 2 || piece === '2' || piece === 'white') {
            whitePlayer = playerInfo
            gameStore.updatePlayerPiece(playerId, 2)
          } else {
            // 如果服务端没有返回 piece，检查 store 中的缓存
            const playerPieces = gameStore.playerPieces
            const storedPiece = playerPieces[playerId]
            if (storedPiece === '1' || storedPiece === 'black') {
              blackPlayer = playerInfo
            } else if (storedPiece === '2' || storedPiece === 'white') {
              whitePlayer = playerInfo
            } else {
              // 暂时放入未分配列表
              unassignedPlayers.push({ info: playerInfo, playerData: p })
            }
          }
        })

        // v2.4.7: 处理未分配的玩家（服务端没有返回 piece 字段的情况）
        // 按照加入顺序分配：创建者黑方，第二个玩家白方
        unassignedPlayers.forEach(({ info, playerData }) => {
          const pId = playerData.playerId || playerData.player_id
          if (!blackPlayer && pId === creatorId) {
            // 创建者默认黑方
            blackPlayer = info
            gameStore.updatePlayerPiece(pId, 1)
            console.log('[GameHandler] Assigned creator to black:', pId)
          } else if (!blackPlayer) {
            // 如果没有黑方，先分配黑方
            blackPlayer = info
            gameStore.updatePlayerPiece(pId, 1)
            console.log('[GameHandler] Assigned first unassigned to black:', pId)
          } else if (!whitePlayer) {
            // 然后分配白方
            whitePlayer = info
            gameStore.updatePlayerPiece(pId, 2)
            console.log('[GameHandler] Assigned second unassigned to white:', pId)
          }
        })

        // 更新 currentGame
        if (blackPlayer || whitePlayer) {
          console.log('[GameHandler] Calling updateGamePlayers with:', { black: blackPlayer, white: whitePlayer })
          gameStore.updateGamePlayers({
            black: blackPlayer,
            white: whitePlayer,
          }, roomId)
          console.log('[GameHandler] Room info - players updated:', { black: blackPlayer, white: whitePlayer })
          console.log('[GameHandler] Current currentGame after update:', gameStore.currentGame)
        } else {
          console.log('[GameHandler] No players to update')
        }
      } else {
        console.log('[GameHandler] No players in room_info data')
      }
    }

    callback?.(message)
  },

  /**
   * 处理棋子分配消息
   */
  handlePlayerPieceAssigned: (message: PlayerPieceAssignedMessage, callback?: MessageHandlerCallback<PlayerPieceAssignedMessage>) => {
    const gameStore = useGameStore()
    const authStore = useAuthStore()
    console.log('[GameHandler] Player piece assigned:', message.data.playerId, message.data.piece)

    // 更新玩家棋子
    gameStore.updatePlayerPiece(message.data.playerId, message.data.piece)

    // 如果是当前玩家，更新我的棋子
    if (message.data.playerId === authStore.user?.id) {
      gameStore.setMyPiece(message.data.piece === 1 ? 'black' : 'white')
    }

    callback?.(message)
  },

  // ==================== 准备状态相关 ====================

  /**
   * 处理准备确认消息
   */
  handleReadyConfirm: (message: ReadyConfirmMessage, callback?: MessageHandlerCallback<ReadyConfirmMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Ready confirm:', message.data.playerId, message.data.ready)

    // 更新玩家准备状态
    gameStore.updatePlayerReadyState(message.data.playerId, message.data.ready)

    callback?.(message)
  },

  /**
   * 处理玩家准备状态变化
   */
  handlePlayerReady: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Player ready:', message.data?.playerId, message.data?.ready)

    if (message.data?.playerId && message.data?.ready !== undefined) {
      gameStore.updatePlayerReadyState(message.data.playerId, message.data.ready)
    }

    callback?.(message)
  },

  /**
   * 处理所有玩家准备状态
   */
  handleAllPlayersReady: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] All players ready:', message.data?.players, message.data?.canStart)

    // 更新所有玩家准备状态
    if (message.data?.players) {
      Object.entries(message.data.players).forEach(([playerId, ready]) => {
        gameStore.updatePlayerReadyState(playerId, ready as boolean)
      })
    }

    callback?.(message)
  },

  // ==================== 游戏状态相关 ====================

  /**
   * 处理游戏状态更新
   * 服务端 v2.2+: playerPieces 返回 { "usr_A": 1, "usr_B": 2 } 格式
   */
  handleGameState: (message: GameStateMessage, callback?: MessageHandlerCallback<GameStateMessage>) => {
    const gameStore = useGameStore()
    const authStore = useAuthStore()
    console.log('[GameHandler] Game state:', message.data)

    // 更新游戏状态
    gameStore.updateGameState({
      board: message.data.board.board,
      currentPlayer: message.data.board.currentPlayer,
      gameResult: message.data.board.gameResult,
      lastMove: message.data.board.lastMove,
      totalMoves: message.data.board.totalMoves,
      blackTimeLeft: message.data.board.blackTimeLeft,
      whiteTimeLeft: message.data.board.whiteTimeLeft,
      config: message.data.config,
      playerPieces: message.data.playerPieces,
      spectatorCount: message.data.spectatorCount,
    })

    // 如果 playerPieces 不为空，更新 currentGame 的玩家信息
    if (message.data.playerPieces && Object.keys(message.data.playerPieces).length > 0) {
      const playerPieces = message.data.playerPieces
      const currentGame = gameStore.currentGame
      const currentRoomInfo = gameStore.currentRoomInfo
      const myId = authStore.user?.id

      let blackPlayer: any = null
      let whitePlayer: any = null

      // 遍历 playerPieces 确定黑白方
      Object.entries(playerPieces).forEach(([playerId, piece]) => {
        const pieceNum = typeof piece === 'number' ? piece : parseInt(piece as string)
        const playerInfo = {
          id: playerId,
          username: playerId === myId ? authStore.user?.username : (playerId.split('_')[0] || '玩家'),
          rating: playerId === myId ? (authStore.user as any)?.rating : 1000,
        }

        if (pieceNum === 1) {
          blackPlayer = playerInfo
        } else if (pieceNum === 2) {
          whitePlayer = playerInfo
        }
      })

      // 如果 currentGame 中已有玩家信息，保留其 username 和 rating
      if (currentGame?.players) {
        if (currentGame.players.black && blackPlayer) {
          blackPlayer = {
            ...blackPlayer,
            username: currentGame.players.black.username || blackPlayer.username,
            rating: currentGame.players.black.rating || blackPlayer.rating,
          }
        }
        if (currentGame.players.white && whitePlayer) {
          whitePlayer = {
            ...whitePlayer,
            username: currentGame.players.white.username || whitePlayer.username,
            rating: currentGame.players.white.rating || whitePlayer.rating,
          }
        }
      }

      if (blackPlayer || whitePlayer) {
        gameStore.updateGamePlayers({
          black: blackPlayer,
          white: whitePlayer,
        }, currentGame?.id || currentRoomInfo?.id)
        console.log('[GameHandler] Game state - players updated from playerPieces:', { black: blackPlayer, white: whitePlayer })
      }
    }

    callback?.(message)
  },

  /**
   * 处理落子结果
   */
  handleMoveResult: (message: MoveResultMessage, callback?: MessageHandlerCallback<MoveResultMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Move result:', message.data)

    if (message.data.success) {
      // 更新棋盘
      gameStore.updateBoardAfterMove(message.data.position)
    }

    callback?.(message)
  },

  /**
   * 处理轮次变化
   */
  handleTurnChanged: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Turn changed:', message.data?.current_player)

    if (message.data?.current_player) {
      gameStore.updateCurrentPlayer(message.data.current_player)
    }

    callback?.(message)
  },

  // ==================== 游戏结束相关 ====================

  /**
   * 处理游戏结束消息
   * v2.0.3: 包含成就、排行榜变化、奖励汇总
   */
  handleGameEnd: (message: GameEndMessage, callback?: MessageHandlerCallback<GameEndMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Game end:', message.data)

    // 更新游戏结果
    gameStore.setGameResult({
      result: String(message.data.result),
      winnerPiece: message.data.winnerPiece != null ? String(message.data.winnerPiece) : undefined,
      gameStats: message.data.gameStats,
    })

    // 如果有成就解锁（扩展字段）
    if ((message.data as any).achievements) {
      console.log('[GameHandler] Achievements unlocked:', (message.data as any).achievements)
      gameStore.setUnlockedAchievements((message.data as any).achievements)
    }

    // 如果有排行榜变化（扩展字段）
    if ((message.data as any).leaderboard_changes) {
      console.log('[GameHandler] Leaderboard changes:', (message.data as any).leaderboard_changes)
      gameStore.setLeaderboardChanges((message.data as any).leaderboard_changes)
    }

    // 如果有奖励（扩展字段）
    if ((message.data as any).rewards) {
      console.log('[GameHandler] Rewards:', (message.data as any).rewards)
      gameStore.setGameRewards((message.data as any).rewards)

      // 更新货币（如果有金币奖励）
      const rewards = (message.data as any).rewards
      if (rewards.gold) {
        // 货币更新由 currency_update 消息处理，这里只是记录
        console.log('[GameHandler] Gold reward:', rewards.gold)
      }
    }

    callback?.(message)
  },

  /**
   * 转换 WebSocket 结算数据为标准 GameSettlementData 格式
   *
   * WebSocket 消息格式（嵌套结构）:
   * {
   *   result: "win" | "lose" | "draw",  // 小写
   *   rating: { before, after, change },
   *   tier: { before, after, name_before, name_after },  // 数字
   *   rewards: { gold, experience, honor }
   * }
   *
   * 文档标准格式（扁平结构）:
   * {
   *   result: "WIN" | "LOSS" | "DRAW",  // 大写
   *   rating_before, rating_after, rating_change,
   *   tier_before: "SILVER_I", tier_after: "SILVER_I",  // 字符串枚举
   *   reward: { experience, currency_rewards: [...] }
   * }
   */
  transformSettlementData: (wsData: any): GameSettlementData => {
    // 结果转换：小写 -> 大写
    const resultMap: Record<string, 'WIN' | 'LOSS' | 'DRAW' | 'SURRENDER' | 'TIMEOUT' | 'DISCONNECT'> = {
      win: 'WIN',
      lose: 'LOSS',
      loss: 'LOSS',
      draw: 'DRAW',
      surrender: 'SURRENDER',
      timeout: 'TIMEOUT',
      disconnect: 'DISCONNECT',
    }
    const result = resultMap[wsData.result?.toLowerCase()] || 'DRAW'

    // 段位数字转字符串枚举
    const tierToTierString = (tier: number | string | undefined): TierString => {
      if (typeof tier === 'string') return tier as TierString
      const tierMap: Record<number, TierString> = {
        0: 'BRONZE_III', 1: 'BRONZE_II', 2: 'BRONZE_I',
        3: 'SILVER_III', 4: 'SILVER_II', 5: 'SILVER_I',
        6: 'GOLD_III', 7: 'GOLD_II', 8: 'GOLD_I',
        9: 'PLATINUM_III', 10: 'PLATINUM_II', 11: 'PLATINUM_I',
        12: 'DIAMOND_III', 13: 'DIAMOND_II', 14: 'DIAMOND_I',
        15: 'MASTER',
      }
      return tierMap[tier ?? 0] || 'BRONZE_III'
    }

    const tierBefore = tierToTierString(wsData.tier?.before)
    const tierAfter = tierToTierString(wsData.tier?.after)
    const tierChanged = wsData.tier?.before !== wsData.tier?.after
    const tierBeforeNum = typeof wsData.tier?.before === 'number' ? wsData.tier.before : 0
    const tierAfterNum = typeof wsData.tier?.after === 'number' ? wsData.tier.after : 0

    // 构建货币奖励数组
    const currencyRewards: Array<{ type: CurrencyType; amount: number }> = []
    if (wsData.rewards?.gold > 0) {
      currencyRewards.push({ type: 'gold', amount: wsData.rewards.gold })
    }
    if (wsData.rewards?.gem > 0 || wsData.rewards?.gems > 0) {
      currencyRewards.push({ type: 'gem', amount: wsData.rewards.gem || wsData.rewards.gems })
    }
    if (wsData.rewards?.honor > 0 || wsData.rewards?.honor_points > 0) {
      currencyRewards.push({ type: 'honor', amount: wsData.rewards.honor || wsData.rewards.honor_points })
    }

    return {
      user_id: wsData.user_id || '',
      result,
      // 扁平化评分数据
      rating_before: wsData.rating?.before ?? 0,
      rating_after: wsData.rating?.after ?? 0,
      rating_change: wsData.rating?.change ?? 0,
      // 段位字符串
      tier_before: tierBefore,
      tier_after: tierAfter,
      tier_changed: tierChanged,
      tier_promoted: tierAfterNum > tierBeforeNum,
      tier_demoted: tierAfterNum < tierBeforeNum,
      // 奖励（新格式）
      reward: {
        experience: wsData.rewards?.experience ?? 0,
        currency_rewards: currencyRewards,
      },
      // 统计更新
      new_win_streak: wsData.stats?.new_win_streak ?? wsData.new_win_streak ?? 0,
      new_best_win_streak: wsData.stats?.new_best_win_streak ?? wsData.new_best_win_streak,
      new_games_played: wsData.stats?.new_games_played ?? wsData.new_games_played,
      new_wins: wsData.stats?.new_wins ?? wsData.new_wins,
      new_losses: wsData.stats?.new_losses ?? wsData.new_losses,
      new_draws: wsData.stats?.new_draws ?? wsData.new_draws,
      new_level: wsData.stats?.new_level ?? wsData.new_level,
      // 成就
      achievements_unlocked: wsData.achievements_unlocked || [],
      // 向后兼容字段（保留原始嵌套结构供旧组件使用）
      rating: wsData.rating,
      tier: wsData.tier,
      rewards: wsData.rewards,
      achievements_unlocked_detailed: wsData.achievements_unlocked_detailed,
    }
  },

  /**
   * 处理游戏结算消息
   *
   * 文档依据: game_platform_gameplay_design.md
   * - §7.3 GameEndEvent 事件定义
   * - §16.2.4 消息 8 game_settlement
   *
   * 功能：
   * - 更新玩家评分和段位信息
   * - 显示奖励信息
   * - 处理成就解锁
   * - 更新连胜统计
   */
  handleGameSettlement: (message: GameSettlementMessage, callback?: MessageHandlerCallback<GameSettlementMessage>) => {
    const gameStore = useGameStore()
    const gameDataStore = useGameDataStore()
    const authStore = useAuthStore()
    const rawData = message.data

    console.log('[GameHandler] ===== Game settlement received =====')
    console.log('[GameHandler] Message type:', message?.type)
    console.log('[GameHandler] Raw settlement data:', rawData)
    console.log('[GameHandler] Rating change:', {
      before: rawData?.rating?.before,
      after: rawData?.rating?.after,
      change: rawData?.rating?.change
    })
    console.log('[GameHandler] Tier change:', {
      before: rawData?.tier?.before,
      after: rawData?.tier?.after,
      name_before: rawData?.tier?.name_before,
      name_after: rawData?.tier?.name_after
    })
    console.log('[GameHandler] Rewards:', rawData?.rewards)
    console.log('[GameHandler] Achievements:', rawData?.achievements_unlocked)
    console.log('[GameHandler] Stats:', rawData?.stats)

    // 转换 WebSocket 数据为标准格式
    const data = gameHandlers.transformSettlementData(rawData)
    console.log('[GameHandler] Transformed settlement data:', data)

    // 更新结算数据到 store，触发结算弹窗显示
    gameStore.setSettlement(data)

    // 如果有奖励，更新货币（使用向后兼容的 rewards 字段）
    if (rawData.rewards) {
      console.log('[GameHandler] Settlement rewards:', rawData.rewards)
      // 实时更新金币（如果货币更新消息没先到达）
      if (rawData.rewards.gold > 0) {
        gameDataStore.updateCurrencyRealtime('gold', rawData.rewards.gold, rawData.rewards.gold)
      }
      if (rawData.rewards.honor > 0 || (rawData.rewards.honor_points || 0) > 0) {
        const honorAmount = rawData.rewards.honor || rawData.rewards.honor_points || 0
        gameDataStore.updateCurrencyRealtime('honor', honorAmount, honorAmount)
      }
    }

    // 如果有成就解锁，添加到通知队列
    if (data.achievements_unlocked && data.achievements_unlocked.length > 0) {
      console.log('[GameHandler] Settlement achievements:', data.achievements_unlocked)
      data.achievements_unlocked.forEach((achievement) => {
        // 兼容字符串和对象两种格式
        const achievementId = typeof achievement === 'string' ? achievement : achievement.id
        const achievementName = typeof achievement === 'string' ? achievement : achievement.name
        const achievementRarity = typeof achievement === 'string' ? 'COMMON' : achievement.rarity

        gameStore.addAchievementNotification({
          achievement_id: achievementId,
          achievement_name: achievementName,
          rarity: achievementRarity,
          points: 0, // 结算消息不包含 points
          reward: undefined,
        })
      })

      // 重新加载成就数据
      if (authStore.user?.id) {
        gameDataStore.loadAchievements(authStore.user.id)
      }
    }

    // 如果有段位变化，记录日志
    if (data.tier_changed) {
      console.log('[GameHandler] Tier changed:', {
        before: data.tier_before,
        after: data.tier_after,
        ratingChange: data.rating_change,
      })
    }

    // 如果有排行榜变化，更新排行榜显示
    if (rawData.leaderboard_rank_change) {
      console.log('[GameHandler] Leaderboard rank changed:', rawData.leaderboard_rank_change)
      gameStore.setLeaderboardChanges({
        type: 'global',
        scope: 'rating',
        old_rank: rawData.leaderboard_rank_change.old_rank,
        new_rank: rawData.leaderboard_rank_change.new_rank,
        score: data.rating_after,
      })
    }

    // 更新玩家档案（评分、段位等）
    if (authStore.user?.id === data.user_id) {
      // 重新加载玩家档案数据
      gameDataStore.loadGameProfile(data.user_id)
    }

    callback?.(message)
  },

  // ==================== 匹配系统相关 v2.6.0 ====================

  /**
   * 处理匹配状态更新消息
   *
   * 文档依据: game_platform_gameplay_design.md
   * - §6 匹配系统
   * - §16.2.4 匹配状态更新消息
   */
  handleMatchStatusUpdate: (message: MatchStatusUpdateMessage, callback?: MessageHandlerCallback<MatchStatusUpdateMessage>) => {
    const matchStore = useMatchStore()
    console.log('[GameHandler] Match status update:', message.data)

    matchStore.updateMatchStatus({
      status: message.data.status as any,
      queue_size: message.data.queue_size,
      estimated_wait_time: message.data.estimated_wait_time,
      match_id: message.data.match_id,
    })

    callback?.(message)
  },

  /**
   * 处理匹配成功消息
   *
   * 文档依据: game_platform_gameplay_design.md
   * - §6 匹配系统
   * - §16.2.4 匹配成功消息
   */
  handleMatchFound: (message: MatchFoundMessage, callback?: MessageHandlerCallback<MatchFoundMessage>) => {
    const matchStore = useMatchStore()
    const gameStore = useGameStore()
    console.log('[GameHandler] Match found:', message.data)

    // 更新匹配结果
    matchStore.setMatchFound({
      match_id: message.data.match_id,
      room_id: message.data.room_id,
      opponent: message.data.opponent,
      your_piece: message.data.your_piece,
      config: message.data.config,
    })

    // 更新游戏配置
    if (message.data.config) {
      gameStore.updateGameConfig({
        gameMode: 0, // 从 config 解析
        gameModeStr: message.data.config.game_mode,
        timeLimit: message.data.config.time_limit,
        incrementPerMove: message.data.config.increment_per_move,
      })
    }

    callback?.(message)
  },

  /**
   * 处理匹配超时消息
   *
   * 文档依据: game_platform_gameplay_design.md
   * - §6 匹配系统
   * - §16.2.4 匹配超时消息
   */
  handleMatchTimeout: (message: MatchTimeoutMessage, callback?: MessageHandlerCallback<MatchTimeoutMessage>) => {
    const matchStore = useMatchStore()
    console.log('[GameHandler] Match timeout:', message.data)

    matchStore.setMatchTimeout(
      message.data.reason,
      message.data.wait_time,
      message.data.can_retry
    )

    callback?.(message)
  },

  // ==================== 成就和排行榜相关 ====================

  /**
   * 处理成就解锁实时通知
   */
  handleAchievementUnlocked: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    const gameDataStore = useGameDataStore()
    console.log('[GameHandler] Achievement unlocked:', message.data)

    // 添加到成就解锁队列
    gameStore.addAchievementNotification({
      achievement_id: message.data?.achievement_id,
      achievement_name: message.data?.achievement_name,
      rarity: message.data?.rarity,
      points: message.data?.points,
      reward: message.data?.reward,
    })

    // 重新加载成就数据
    const authStore = useAuthStore()
    if (authStore.user?.id) {
      gameDataStore.loadAchievements(authStore.user.id)
    }

    callback?.(message)
  },

  /**
   * 处理排行榜更新实时通知
   */
  handleLeaderboardUpdate: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Leaderboard update:', message.data)

    // 更新排行榜变化显示
    gameStore.setLeaderboardChanges({
      type: message.data?.type,
      scope: message.data?.scope,
      old_rank: message.data?.old_rank,
      new_rank: message.data?.new_rank,
      score: message.data?.score,
    })

    callback?.(message)
  },

  // ==================== 货币相关 ====================

  /**
   * 处理货币更新
   */
  handleCurrencyUpdate: (message: any, callback?: MessageHandlerCallback) => {
    const gameDataStore = useGameDataStore()
    console.log('[GameHandler] Currency update:', message.data)

    // 实时更新货币（不需要重新加载）
    if (message.data?.currencyType && message.data?.newAmount !== undefined) {
      gameDataStore.updateCurrencyRealtime(
        message.data.currencyType,
        message.data.newAmount,
        message.data.change
      )
    }

    callback?.(message)
  },

  // ==================== 时间相关 ====================

  /**
   * 处理时间更新
   */
  handleTimeUpdate: (message: TimeUpdateMessage, callback?: MessageHandlerCallback<TimeUpdateMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Time update:', message.data)

    gameStore.updateTime({
      blackTime: message.data.blackTime,
      whiteTime: message.data.whiteTime,
    })

    callback?.(message)
  },

  // ==================== 悔棋相关 ====================

  /**
   * 处理悔棋请求通知
   */
  handleUndoRequest: (message: UndoRequestMessage, callback?: MessageHandlerCallback<UndoRequestMessage>) => {
    console.log('[GameHandler] Undo request from:', message.data.fromPlayer)

    // 可以在这里显示悔棋请求对话框
    // 例如使用 ElMessageBox.confirm

    callback?.(message)
  },

  /**
   * 处理悔棋成功消息
   */
  handleMoveUndone: (message: MoveUndoneMessage, callback?: MessageHandlerCallback<MoveUndoneMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Move undone:', message.data.playerId, message.data.totalMoves)

    // 更新棋盘状态
    gameStore.updateBoard(message.data.board, message.data.totalMoves)

    callback?.(message)
  },

  // ==================== 认输相关 ====================

  /**
   * 处理玩家认输消息
   */
  handlePlayerSurrendered: (message: PlayerSurrenderedMessage, callback?: MessageHandlerCallback<PlayerSurrenderedMessage>) => {
    console.log('[GameHandler] Player surrendered:', message.data.playerId, message.data.piece)

    // 可以在这里显示认输通知

    callback?.(message)
  },

  // ==================== 社交相关 ====================

  /**
   * 处理聊天消息广播
   */
  handleChat: (message: ChatBroadcastMessage, callback?: MessageHandlerCallback<ChatBroadcastMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Chat:', message.data)

    gameStore.addChatMessage({
      playerId: message.data.playerId,
      message: message.data.message,
      timestamp: message.data.timestamp,
    })

    callback?.(message)
  },

  /**
   * 处理观战者变化
   */
  handleSpectatorChange: (message: SpectatorChangeMessage, callback?: MessageHandlerCallback<SpectatorChangeMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Spectator change:', message.data)

    gameStore.updateSpectatorCount(message.data.spectatorCount)

    callback?.(message)
  },

  /**
   * 处理系统通知
   */
  handleNotification: (message: any, callback?: MessageHandlerCallback) => {
    console.log('[GameHandler] Notification:', message.data)
    callback?.(message)
  },

  // ==================== 玩家相关 ====================

  /**
   * 处理玩家加入
   */
  handlePlayerJoined: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    const data = message.data

    console.log('[GameHandler] Player joined:', data)

    // 兼容两种格式：player 或 playerInfo
    const player = data.player || data.playerInfo

    if (player) {
      // 处理 pieceType：1 -> 'black', 2 -> 'white'
      let color = data.color || data.pieceType
      if (color === 1 || color === '1') {
        color = 'black'
      } else if (color === 2 || color === '2') {
        color = 'white'
      }

      // 构建玩家对象
      const playerData = {
        id: player.player_id || player.id || data.playerId,
        username: player.username || player.nickname || data.playerId?.split('_')[0] || '玩家',
        rating: player.rating || 1000,
      }

      console.log('[GameHandler] Adding player to room:', playerData, 'color:', color)
      gameStore.addPlayerToRoom(playerData, color)
    }

    callback?.(message)
  },

  /**
   * 处理玩家离开
   * 服务端返回格式：{ playerId, playerCount, roomId }
   */
  handlePlayerLeft: (message: any, callback?: MessageHandlerCallback) => {
    const gameStore = useGameStore()
    const data = message.data || {}

    console.log('[GameHandler] Player left:', data)

    // 兼容两种格式：playerId 或 player.id
    const playerId = data.playerId || data.player?.id
    const playerCount = data.playerCount

    if (playerId) {
      gameStore.removePlayerFromRoom(playerId)
      console.log('[GameHandler] Removed player from room:', playerId, 'remaining:', playerCount)
    }

    callback?.(message)
  },

  // ==================== 错误处理 ====================

  /**
   * 处理错误消息
   */
  handleError: (message: ErrorMessage) => {
    console.error('[GameHandler] Error:', message.error_code, message.error_message)

    // 可以在这里添加全局错误处理逻辑
    // 例如：显示错误提示、记录日志等
  },

  // ==================== v2.1.0 新增：房间列表相关 ====================

  /**
   * 处理房间列表消息
   */
  handleRoomList: (message: RoomListMessage, callback?: MessageHandlerCallback<RoomListMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Room list received:', message.data.rooms?.length, 'rooms')

    // 更新完整的房间列表
    if (message.data.rooms) {
      gameStore.setRooms(message.data.rooms)
    }

    callback?.(message)
  },

  /**
   * 处理订阅确认消息
   */
  handleSubscriptionConfirm: (message: SubscriptionConfirmMessage, callback?: MessageHandlerCallback<SubscriptionConfirmMessage>) => {
    console.log('[GameHandler] Subscription confirm:', message.data.topic, message.data.subscribed)

    // 如果订阅成功且有初始房间列表
    if (message.data.subscribed && message.data.rooms) {
      const gameStore = useGameStore()
      gameStore.setRooms(message.data.rooms)
    }

    callback?.(message)
  },

  /**
   * 处理新房间创建通知
   */
  handleRoomAdded: (message: RoomAddedMessage, callback?: MessageHandlerCallback<RoomAddedMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Room added:', message.data.room?.room_id)

    if (message.data.room) {
      gameStore.addRoom(message.data.room)
    }

    callback?.(message)
  },

  /**
   * 处理房间更新通知
   * 服务端可能返回多种格式：
   * 1. { roomId: "...", changes: {...完整房间数据...} } - 实际服务端返回
   * 2. { room_id: "...", changes: {...} } - 按文档定义
   * 3. { 完整房间对象 } - 直接是房间数据
   */
  handleRoomUpdated: (message: any, callback?: MessageHandlerCallback<any>) => {
    const gameStore = useGameStore()
    const data = message.data

    console.log('[GameHandler] Room updated message:', data)

    // 格式 1: { roomId: "...", changes: {...完整房间数据...} }
    if (data.roomId && data.changes) {
      // changes 包含完整房间数据
      const roomData = data.changes
      const roomId = roomData.room_id || data.roomId
      console.log('[GameHandler] Room updated (format 1):', roomId)
      if (roomData.room_id) {
        gameStore.updateRoomFromFullObject(roomData)
      }
    }
    // 格式 2: { room_id: "...", changes: {...} } - 无 roomId 包装
    else if (data.room_id && !data.roomId) {
      if (data.changes) {
        // changes 包含部分更新
        console.log('[GameHandler] Room updated (format 2):', data.room_id, data.changes)
        gameStore.updateRoom(data.room_id, data.changes)
      } else {
        // 完整房间对象
        console.log('[GameHandler] Room updated (format 3 - full):', data.room_id)
        gameStore.updateRoomFromFullObject(data)
      }
    }
    // 格式 3: 直接是完整房间对象（无 changes）
    else if (data.room_id || data.roomId) {
      const roomData = data.changes || data
      const roomId = roomData.room_id || data.roomId || data.room_id
      console.log('[GameHandler] Room updated (fallback):', roomId)
      if (roomData.room_id) {
        gameStore.updateRoomFromFullObject(roomData)
      }
    }

    callback?.(message)
  },

  /**
   * 处理房间移除通知
   * 兼容两种字段名：roomId (文档定义 camelCase) 和 room_id (实际 snake_case)
   */
  handleRoomRemoved: (message: any, callback?: MessageHandlerCallback<any>) => {
    const gameStore = useGameStore()

    // 兼容两种字段名
    const roomId = message.data.roomId || message.data.room_id
    console.log('[GameHandler] Room removed:', roomId)

    if (roomId) {
      gameStore.removeRoom(roomId)
    }

    callback?.(message)
  },

  // ==================== v2.1.0 新增：房间操作结果 ====================

  /**
   * 处理房间创建结果
   * 兼容两种字段名格式：roomId (camelCase) 和 room_id (snake_case)
   * v2.5.5: 构造 roomInfo 对象，确保创建者能看到房间人数
   */
  handleRoomCreated: (message: any, callback?: MessageHandlerCallback<any>) => {
    const gameStore = useGameStore()
    const authStore = useAuthStore()

    // 兼容两种字段名
    const roomId = message.data.roomId || message.data.room_id
    const pieceType = message.data.pieceType || message.data.piece_type
    const creatorId = message.data.creatorId || message.data.creator_id

    console.log('[GameHandler] Room created:', roomId, pieceType, creatorId)

    if (!roomId) {
      console.error('[GameHandler] Room created but no roomId found')
      callback?.(message)
      return
    }

    // v2.5.5: 构造 roomInfo 对象，包含创建者作为第一个玩家
    const creatorInfo = {
      playerId: creatorId,
      piece: pieceType === 'black' ? 1 : (pieceType === 'white' ? 2 : 1),
      username: authStore.user?.username || creatorId?.split('_')[0] || '玩家',
      nickname: (authStore.user as any)?.nickname || authStore.user?.username || '玩家',
      rating: (authStore.user as any)?.rating || 1000,
      level: 1,
      state: 1,
      isQt6: false,
    }

    const roomInfo = {
      roomId: roomId,
      creatorId: creatorId,
      gameType: 3, // gomoku
      roomState: 0, // waiting
      currentPlayers: 1, // 创建者是第一个玩家
      maxPlayers: 2,
      spectatorCount: 0,
      players: [creatorInfo],
      createdAt: Date.now(),
      lastActivity: Date.now(),
    }

    // 更新当前房间信息
    gameStore.setCurrentRoom({
      id: roomId,
      isCreator: true,
      pieceType: pieceType,
      config: message.data.config,
      roomInfo: roomInfo, // v2.5.5: 添加 roomInfo
    })

    console.log('[GameHandler] Room created with roomInfo:', roomInfo)

    // 设置我的棋子
    if (pieceType) {
      gameStore.setMyPiece(pieceType)
    }

    // 更新游戏玩家信息 - 创建者是黑方或白方
    if (creatorId && pieceType) {
      const playerInfo = {
        id: creatorId,
        username: authStore.user?.username || creatorId.split('_')[0] || '玩家',
        rating: (authStore.user as any)?.rating || 1000,
      }

      gameStore.updateGamePlayers({
        black: pieceType === 'black' ? playerInfo : null,
        white: pieceType === 'white' ? playerInfo : null,
      }, roomId)
      console.log('[GameHandler] Game players updated:', pieceType, playerInfo)
    }

    callback?.(message)
  },

  /**
   * 处理加入房间结果
   * v2.5.3: 更新以支持完整的 roomInfo 处理
   */
  handleRoomJoined: (message: any, callback?: MessageHandlerCallback<any>) => {
    const gameStore = useGameStore()
    const authStore = useAuthStore()
    const data = message.data || {}

    console.log('[GameHandler] Room joined:', data.roomId, data.pieceType)

    // 检查当前用户是否是创建者（通过 roomInfo.creatorId 判断）
    const isCreator = data.roomInfo?.creatorId === authStore.user?.id ||
                      data.roomInfo?.creator_id === authStore.user?.id

    // 更新当前房间信息
    gameStore.setCurrentRoom({
      id: data.roomId,
      isCreator,
      pieceType: data.pieceType,
      roomInfo: data.roomInfo,
    })

    // 设置我的棋子（优先使用顶层 pieceType）
    if (data.pieceType) {
      gameStore.setMyPiece(data.pieceType)
      console.log('[GameHandler] My piece set to:', data.pieceType)
    }

    // v2.5.3: 如果 roomInfo 中有玩家信息，也更新玩家显示
    // v2.5.4: 兼容 camelCase 和 snake_case
    if (data.roomInfo?.players && data.roomInfo.players.length > 0) {
      console.log('[GameHandler] Processing players from roomInfo:', data.roomInfo.players)

      let blackPlayer: any = null
      let whitePlayer: any = null

      data.roomInfo.players.forEach((p: any) => {
        // v2.5.4: 兼容 playerId 和 player_id
        const pId = p.playerId || p.player_id || p.id
        const playerInfo = {
          id: pId,
          username: p.username || p.nickname || pId?.split('_')[0] || '玩家',
          rating: p.rating || 1000,
        }

        // 使用顶层 pieceType 或者 players 中的 piece 字段
        const piece = p.piece
        if (piece === 1 || piece === '1' || piece === 'black') {
          blackPlayer = playerInfo
          gameStore.updatePlayerPiece(pId, 1)
        } else if (piece === 2 || piece === '2' || piece === 'white') {
          whitePlayer = playerInfo
          gameStore.updatePlayerPiece(pId, 2)
        }
      })

      // 如果当前玩家是加入者，根据顶层 pieceType 分配
      const myId = authStore.user?.id
      if (data.pieceType && myId) {
        const myInfo = {
          id: myId,
          username: authStore.user?.username || myId.split('_')[0] || '玩家',
          rating: (authStore.user as any)?.rating || 1000,
        }
        if (data.pieceType === 'black' && !blackPlayer) {
          blackPlayer = myInfo
          gameStore.updatePlayerPiece(myId, 1)
        } else if (data.pieceType === 'white' && !whitePlayer) {
          whitePlayer = myInfo
          gameStore.updatePlayerPiece(myId, 2)
        }
      }

      if (blackPlayer || whitePlayer) {
        gameStore.updateGamePlayers({
          black: blackPlayer,
          white: whitePlayer,
        }, data.roomId)
        console.log('[GameHandler] Room joined - players updated:', { black: blackPlayer, white: whitePlayer })
      }
    }

    callback?.(message)
  },

  /**
   * 处理离开房间结果
   */
  handleRoomLeft: (message: RoomLeftMessage, callback?: MessageHandlerCallback<RoomLeftMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Room left:', message.data.roomId)

    // 清理房间状态
    gameStore.clearCurrentRoom()

    callback?.(message)
  },

  /**
   * 处理操作结果消息
   * v2.3.0: 处理 room_operation 的响应
   */
  handleOperationResult: (message: any, callback?: MessageHandlerCallback) => {
    console.log('[GameHandler] Operation result:', message.data)

    const { operation, success, error, message: msg } = message.data || {}

    if (operation === 'start_game') {
      if (success) {
        console.log('[GameHandler] Start game request accepted, waiting for game_started broadcast')
      } else {
        console.error('[GameHandler] Start game failed:', error, msg)
        // 可以通过 callback 传递错误信息给调用者
      }
    }

    callback?.(message)
  },

  /**
   * 处理游戏开始广播
   */
  handleGameStarted: (message: GameStartedMessage, callback?: MessageHandlerCallback<GameStartedMessage>) => {
    const gameStore = useGameStore()
    console.log('[GameHandler] Game started:', message.roomId)

    // 更新游戏状态
    gameStore.setGameStarted({
      board: message.data.board,
      currentPlayer: message.data.currentPlayer,
      blackPlayer: message.data.blackPlayer,
      whitePlayer: message.data.whitePlayer,
      blackTimeLeft: message.data.blackTimeLeft,
      whiteTimeLeft: message.data.whiteTimeLeft,
    })

    callback?.(message)
  },

  // ==================== 通用处理 ====================

  /**
   * 通用消息处理器
   * 根据消息类型自动路由到对应的处理器
   */
  handleMessage: (message: ServerMessage, callbacks?: Record<string, MessageHandlerCallback>) => {
    const handlers: Record<string, (msg: any, cb?: any) => void> = {
      welcome: gameHandlers.handleWelcome,
      auth_success: gameHandlers.handleAuthSuccess,
      auth_failed: gameHandlers.handleAuthFailed,
      room_config: gameHandlers.handleRoomConfig,
      room_info: gameHandlers.handleRoomInfo,
      player_piece_assigned: gameHandlers.handlePlayerPieceAssigned,
      ready_confirm: gameHandlers.handleReadyConfirm,
      player_ready: gameHandlers.handlePlayerReady,
      all_players_ready: gameHandlers.handleAllPlayersReady,
      game_state: gameHandlers.handleGameState,
      current_state: gameHandlers.handleGameState, // current_state 与 game_state 格式类似
      move_result: gameHandlers.handleMoveResult,
      turn_changed: gameHandlers.handleTurnChanged,
      game_end: gameHandlers.handleGameEnd,
      undo_request: gameHandlers.handleUndoRequest,
      move_undone: gameHandlers.handleMoveUndone,
      player_surrendered: gameHandlers.handlePlayerSurrendered,
      achievement_unlocked: gameHandlers.handleAchievementUnlocked,
      leaderboard_update: gameHandlers.handleLeaderboardUpdate,
      currency_update: gameHandlers.handleCurrencyUpdate,
      time_update: gameHandlers.handleTimeUpdate,
      chat: gameHandlers.handleChat,
      spectator_change: gameHandlers.handleSpectatorChange,
      notification: gameHandlers.handleNotification,
      player_joined: gameHandlers.handlePlayerJoined,
      player_left: gameHandlers.handlePlayerLeft,
      error: gameHandlers.handleError,
      // v2.1.0 新增
      room_list: gameHandlers.handleRoomList,
      subscription_confirm: gameHandlers.handleSubscriptionConfirm,
      room_added: gameHandlers.handleRoomAdded,
      room_updated: gameHandlers.handleRoomUpdated,
      room_removed: gameHandlers.handleRoomRemoved,
      room_created: gameHandlers.handleRoomCreated,
      room_joined: gameHandlers.handleRoomJoined,
      room_left: gameHandlers.handleRoomLeft,
      game_started: gameHandlers.handleGameStarted,
      // v2.3.0 新增
      operation_result: gameHandlers.handleOperationResult,
      game_state_update: gameHandlers.handleGameState,  // 游戏状态更新广播
      // v2.6.0 新增：游戏结算
      game_settlement: gameHandlers.handleGameSettlement,
      // v2.6.0 新增：匹配系统
      match_status_update: gameHandlers.handleMatchStatusUpdate,
      match_found: gameHandlers.handleMatchFound,
      match_timeout: gameHandlers.handleMatchTimeout,
    }

    const handler = handlers[message.type]
    if (handler) {
      handler(message, callbacks?.[message.type])
    } else {
      console.warn('[GameHandler] Unknown message type:', message.type)
    }
  },
}

// 导出类型
export type GameHandlers = typeof gameHandlers
