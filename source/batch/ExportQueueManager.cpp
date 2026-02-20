#include "ExportQueueManager.h"

#include <QThread>
#include <QMutexLocker>
#include <QCoreApplication>

// ============================================================================
// Singleton Instance
// ============================================================================

ExportQueueManager* ExportQueueManager::s_instance = nullptr;
static QMutex s_instanceMutex;

ExportQueueManager* ExportQueueManager::instance()
{
    // Double-checked locking for thread-safe singleton
    if (!s_instance) {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance) {
            s_instance = new ExportQueueManager(qApp);
        }
    }
    return s_instance;
}

// ============================================================================
// ExportQueueManager
// ============================================================================

ExportQueueManager::ExportQueueManager(QObject* parent)
    : QObject(parent)
{
    // Worker thread created on demand
}

ExportQueueManager::~ExportQueueManager()
{
    // Cancel any ongoing work
    cancelAll();
    
    // Clean up thread
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(5000);
        delete m_workerThread;
    }
}

void ExportQueueManager::enqueuePdfExport(const QStringList& bundles, 
                                           const BatchOps::ExportPdfOptions& options)
{
    if (bundles.isEmpty()) {
        return;
    }
    
    ExportJob job;
    job.type = ExportJob::Pdf;
    job.bundles = bundles;
    job.pdfOptions = options;
    
    bool shouldStart = false;
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.enqueue(job);
        // Check if we need to start processing while holding the lock
        if (!m_exporting) {
            shouldStart = true;
        }
    }
    
    // Start processing if not already running
    if (shouldStart) {
        startProcessing();
    }
}

void ExportQueueManager::enqueueSnbxExport(const QStringList& bundles,
                                            const BatchOps::ExportSnbxOptions& options)
{
    if (bundles.isEmpty()) {
        return;
    }
    
    ExportJob job;
    job.type = ExportJob::Snbx;
    job.bundles = bundles;
    job.snbxOptions = options;
    
    bool shouldStart = false;
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.enqueue(job);
        // Check if we need to start processing while holding the lock
        if (!m_exporting) {
            shouldStart = true;
        }
    }
    
    // Start processing if not already running
    if (shouldStart) {
        startProcessing();
    }
}

int ExportQueueManager::queuedJobCount() const
{
    QMutexLocker locker(&m_queueMutex);
    return static_cast<int>(m_queue.size());
}

bool ExportQueueManager::isExporting() const
{
    return m_exporting;
}

void ExportQueueManager::cancelAll()
{
    m_cancelled = true;
    
    // Clear the queue
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.clear();
    }
    
    // Note: Current job will check m_cancelled and stop after current file
}

void ExportQueueManager::startProcessing()
{
    // Create worker thread if needed
    if (!m_workerThread) {
        m_workerThread = new QThread(this);
        m_workerThread->setObjectName("ExportWorkerThread");
    }
    
    processNextJob();
}

void ExportQueueManager::processNextJob()
{
    ExportJob job;
    
    // Get next job from queue
    {
        QMutexLocker locker(&m_queueMutex);
        if (m_queue.isEmpty()) {
            m_exporting = false;
            emit queueEmpty();
            return;
        }
        job = m_queue.dequeue();
    }
    
    m_exporting = true;
    m_cancelled = false;
    
    // Create worker for this job
    ExportWorker* worker = new ExportWorker();
    worker->moveToThread(m_workerThread);
    
    // Set up job
    if (job.type == ExportJob::Pdf) {
        worker->setJob(job.bundles, job.pdfOptions, &m_cancelled);
    } else {
        worker->setJob(job.bundles, job.snbxOptions, &m_cancelled);
    }
    
    // Connect signals
    connect(worker, &ExportWorker::progress, 
            this, &ExportQueueManager::onWorkerProgress);
    connect(worker, &ExportWorker::complete, 
            this, &ExportQueueManager::onWorkerComplete);
    
    // Clean up worker when done
    connect(worker, &ExportWorker::complete, worker, &QObject::deleteLater);
    
    // Start thread if not running
    if (!m_workerThread->isRunning()) {
        m_workerThread->start();
    }
    
    // Start processing (invoke on worker thread)
    QMetaObject::invokeMethod(worker, "process", Qt::QueuedConnection);
}

void ExportQueueManager::onWorkerProgress(const QString& file, int current, int total)
{
    int queued = queuedJobCount();
    emit progressChanged(file, current, total, queued);
}

void ExportQueueManager::onWorkerComplete(const BatchOps::BatchResult& result, 
                                           const QString& outputDir)
{
    m_exporting = false;
    
    // Check if was cancelled
    if (m_cancelled) {
        m_cancelled = false;
        emit exportCancelled(result);
    } else {
        emit jobComplete(result, outputDir);
    }
    
    // Process next job in queue
    QMetaObject::invokeMethod(this, "processNextJob", Qt::QueuedConnection);
}

// ============================================================================
// ExportWorker
// ============================================================================

ExportWorker::ExportWorker(QObject* parent)
    : QObject(parent)
{
}

void ExportWorker::setJob(const QStringList& bundles,
                          const BatchOps::ExportPdfOptions& options,
                          std::atomic<bool>* cancelled)
{
    m_jobType = Pdf;
    m_bundles = bundles;
    m_pdfOptions = options;
    m_cancelled = cancelled;
}

void ExportWorker::setJob(const QStringList& bundles,
                          const BatchOps::ExportSnbxOptions& options,
                          std::atomic<bool>* cancelled)
{
    m_jobType = Snbx;
    m_bundles = bundles;
    m_snbxOptions = options;
    m_cancelled = cancelled;
}

void ExportWorker::process()
{
    if (m_jobType == None || m_bundles.isEmpty()) {
        BatchOps::BatchResult emptyResult;
        emit complete(emptyResult, QString());
        return;
    }
    
    // Create progress callback that emits signals
    BatchOps::ProgressCallback progressCallback = 
        [this](int current, int total, const QString& currentFile, const QString& /*status*/) {
            emit progress(currentFile, current, total);
        };
    
    BatchOps::BatchResult result;
    QString outputDir;
    
    if (m_jobType == Pdf) {
        outputDir = m_pdfOptions.outputPath;
        result = BatchOps::exportPdfBatch(m_bundles, m_pdfOptions, 
                                          progressCallback, m_cancelled);
    } else if (m_jobType == Snbx) {
        outputDir = m_snbxOptions.outputPath;
        result = BatchOps::exportSnbxBatch(m_bundles, m_snbxOptions,
                                           progressCallback, m_cancelled);
    }
    
    emit complete(result, outputDir);
}
