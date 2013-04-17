/* $Id$ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIPopupPane class implementation
 */

/*
 * Copyright (C) 2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include <QMainWindow>
#include <QStatusBar>
#include <QPainter>
#include <QStateMachine>
#include <QPropertyAnimation>
#include <QSignalTransition>

/* GUI includes: */
#include "UIPopupPane.h"
#include "QIDialogButtonBox.h"

/* Other VBox includes: */
#include <VBox/sup.h>

void UIAnimationFramework::installPropertyAnimation(QWidget *pParent, const QByteArray &strPropertyName,
                                                    int iStartValue, int iFinalValue, int iAnimationDuration,
                                                    const char *pSignalForward, const char *pSignalBackward)
{
    /* State-machine: */
    QStateMachine *pStateMachine = new QStateMachine(pParent);
    /* State-machine 'start' state: */
    QState *pStateStart = new QState(pStateMachine);
    /* State-machine 'final' state: */
    QState *pStateFinal = new QState(pStateMachine);

    /* State-machine 'forward' animation: */
    QPropertyAnimation *pForwardAnimation = new QPropertyAnimation(pParent, strPropertyName, pParent);
    pForwardAnimation->setEasingCurve(QEasingCurve(QEasingCurve::InOutCubic));
    pForwardAnimation->setDuration(iAnimationDuration);
    pForwardAnimation->setStartValue(iStartValue);
    pForwardAnimation->setEndValue(iFinalValue);
    /* State-machine 'backward' animation: */
    QPropertyAnimation *pBackwardAnimation = new QPropertyAnimation(pParent, strPropertyName, pParent);
    pBackwardAnimation->setEasingCurve(QEasingCurve(QEasingCurve::InOutCubic));
    pBackwardAnimation->setDuration(iAnimationDuration);
    pBackwardAnimation->setStartValue(iFinalValue);
    pBackwardAnimation->setEndValue(iStartValue);

    /* State-machine state transitions: */
    QSignalTransition *pDefaultToHovered = pStateStart->addTransition(pParent, pSignalForward, pStateFinal);
    pDefaultToHovered->addAnimation(pForwardAnimation);
    QSignalTransition *pHoveredToDefault = pStateFinal->addTransition(pParent, pSignalBackward, pStateStart);
    pHoveredToDefault->addAnimation(pBackwardAnimation);

    /* Initial state is 'start': */
    pStateMachine->setInitialState(pStateStart);
    /* Start hover-machine: */
    pStateMachine->start();
}

QStateMachine* UIAnimationFramework::installPropertyAnimation(QWidget *pTarget, const QByteArray &strPropertyName,
                                                              const QByteArray &strValuePropertyNameStart, const QByteArray &strValuePropertyNameFinal,
                                                              const char *pSignalForward, const char *pSignalBackward,
                                                              bool fReversive /*= false*/, int iAnimationDuration /*= 300*/)
{
    /* State-machine: */
    QStateMachine *pStateMachine = new QStateMachine(pTarget);
    /* State-machine 'start' state: */
    QState *pStateStart = new QState(pStateMachine);
    /* State-machine 'final' state: */
    QState *pStateFinal = new QState(pStateMachine);

    /* State-machine 'forward' animation: */
    QPropertyAnimation *pForwardAnimation = new QPropertyAnimation(pTarget, strPropertyName, pStateMachine);
    pForwardAnimation->setEasingCurve(QEasingCurve(QEasingCurve::InOutCubic));
    pForwardAnimation->setDuration(iAnimationDuration);
    pForwardAnimation->setStartValue(pTarget->property(strValuePropertyNameStart));
    pForwardAnimation->setEndValue(pTarget->property(strValuePropertyNameFinal));
    /* State-machine 'backward' animation: */
    QPropertyAnimation *pBackwardAnimation = new QPropertyAnimation(pTarget, strPropertyName, pStateMachine);
    pBackwardAnimation->setEasingCurve(QEasingCurve(QEasingCurve::InOutCubic));
    pBackwardAnimation->setDuration(iAnimationDuration);
    pBackwardAnimation->setStartValue(pTarget->property(strValuePropertyNameFinal));
    pBackwardAnimation->setEndValue(pTarget->property(strValuePropertyNameStart));

    /* State-machine state transitions: */
    QSignalTransition *pDefaultToHovered = pStateStart->addTransition(pTarget, pSignalForward, pStateFinal);
    pDefaultToHovered->addAnimation(pForwardAnimation);
    QSignalTransition *pHoveredToDefault = pStateFinal->addTransition(pTarget, pSignalBackward, pStateStart);
    pHoveredToDefault->addAnimation(pBackwardAnimation);

    /* Initial state is 'start': */
    pStateMachine->setInitialState(!fReversive ? pStateStart : pStateFinal);
    /* Start hover-machine: */
    pStateMachine->start();
    /* Return machine: */
    return pStateMachine;
}


/* Popup-pane frame prototype class: */
class UIPopupPaneFrame : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(int opacity READ opacity WRITE setOpacity);

signals:

    /* Notifiers: Parent propagation stuff: */
    void sigHoverEnter();
    void sigHoverLeave();
    void sigFocusEnter();
    void sigFocusLeave();

public:

    /* Constructor/destructor: */
    UIPopupPaneFrame(QWidget *pParent = 0);
    ~UIPopupPaneFrame();

private:

    /* Helpers: Prepare/cleanup stuff: */
    void prepare();
    void cleanup();

    /* Handlers: Event stuff: */
    void paintEvent(QPaintEvent *pEvent);

    /* Property: Hover stuff: */
    int opacity() const { return m_iOpacity; }
    void setOpacity(int iOpacity) { m_iOpacity = iOpacity; update(); }

    /* Variables: Hover stuff: */
    const int m_iHoverAnimationDuration;
    const int m_iDefaultOpacity;
    const int m_iHoveredOpacity;
    int m_iOpacity;
};


/* Popup-pane text-pane prototype class: */
class UIPopupPaneTextPane : public QWidget
{
    Q_OBJECT;

signals:

    /* Notifiers: Parent propagation stuff: */
    void sigFocusEnter();
    void sigFocusLeave();

    /* Notifier: Animation stuff: */
    void sigSizeChanged();

public:

    /* Constructor/destructor: */
    UIPopupPaneTextPane(QWidget *pParent = 0);
    ~UIPopupPaneTextPane();

    /* API: Text stuff: */
    void setText(const QString &strText);

    /* API: Set desired width: */
    void setDesiredWidth(int iDesiredWidth);

    /* API: Size-hint stuff: */
    QSize minimumSizeHint() const;
    QSize sizeHint() const;

private slots:

    /* Handlers: Animation stuff: */
    void sltFocusEnter();
    void sltFocusLeave();
    
private:

    /* Helpers: Prepare/cleanup stuff: */
    void prepare();
    void cleanup();

    /* Helper: Content stuff: */
    void prepareContent();

    /* Helper: Size-hint stuff: */
    void updateGeometry();

    /* Handler: Animation stuff: */
    void resizeEvent(QResizeEvent *pEvent);

    /* Variables: Label stuff: */
    QLabel *m_pLabel;
    int m_iDesiredWidth;

    /* Variables: Size-hint stuff: */
    QSize m_minimumSizeHint;
    QSize m_sizeHint;

    /* Variables: Animation stuff: */
    bool m_fFocused;
    QObject *m_pAnimation;
};


UIPopupPane::UIPopupPane(QWidget *pParent, const QString &strId,
                         const QString &strMessage, const QString &strDetails,
                         int iButton1, int iButton2, int iButton3,
                         const QString &strButtonText1, const QString &strButtonText2, const QString &strButtonText3)
    : QWidget(pParent)
    , m_fPolished(false)
    , m_strId(strId)
    , m_iMainLayoutMargin(2), m_iMainFrameLayoutMargin(10), m_iMainFrameLayoutSpacing(5)
    , m_iParentStatusBarHeight(parentStatusBarHeight(pParent))
    , m_strMessage(strMessage), m_strDetails(strDetails)
    , m_iButton1(iButton1), m_iButton2(iButton2), m_iButton3(iButton3)
    , m_strButtonText1(strButtonText1), m_strButtonText2(strButtonText2), m_strButtonText3(strButtonText3)
    , m_iButtonEsc(0)
    , m_fHovered(false)
    , m_fFocused(false)
    , m_pMainFrame(0), m_pTextPane(0), m_pButtonBox(0)
    , m_pButton1(0), m_pButton2(0), m_pButton3(0)
{
    /* Prepare: */
    prepare();
}

UIPopupPane::~UIPopupPane()
{
    /* Cleanup: */
    cleanup();
}

void UIPopupPane::setMessage(const QString &strMessage)
{
    /* Make sure somthing changed: */
    if (m_strMessage == strMessage)
        return;

    /* Fetch new message: */
    m_strMessage = strMessage;
    m_pTextPane->setText(m_strMessage);
}

void UIPopupPane::setDetails(const QString &strDetails)
{
    /* Make sure somthing changed: */
    if (m_strDetails == strDetails)
        return;

    /* Fetch new details: */
    m_strDetails = strDetails;
}

void UIPopupPane::sltAdjustGeomerty()
{
    /* Get parent attributes: */
    const int iWidth = parentWidget()->width();
    const int iHeight = parentWidget()->height();

    /* Resize popup according parent width: */
    resize(iWidth, minimumSizeHint().height());

    /* Move popup according parent: */
    move(0, iHeight - height() - m_iParentStatusBarHeight);

    /* Raise popup according parent: */
    raise();

    /* Update layout: */
    updateLayout();
}

void UIPopupPane::prepare()
{
    /* Install event-filter to parent: */
    parent()->installEventFilter(this);
    /* Prepare content: */
    prepareContent();
}

void UIPopupPane::cleanup()
{
}

bool UIPopupPane::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* If its parent event came: */
    if (pWatched == parent())
    {
        /* Make sure its resize event came: */
        if (pEvent->type() != QEvent::Resize)
            return false;

        /* Adjust geometry: */
        adjustGeometry();
    }
    /* Other objects subscribed for hovering: */
    else
    {
        /* Depending on event-type: */
        switch (pEvent->type())
        {
            /* Something is hovered: */
            case QEvent::HoverEnter:
            case QEvent::Enter:
            {
                if (!m_fHovered)
                {
                    m_fHovered = true;
                    emit sigHoverEnter();
                }
                break;
            }
            /* Nothing is hovered: */
            case QEvent::Leave:
            {
                if (pWatched == this && m_fHovered)
                {
                    m_fHovered = false;
                    emit sigHoverLeave();
                }
                break;
            }
            case QEvent::FocusIn:
            {
                if (!m_fFocused)
                {
                    m_fFocused = true;
                    emit sigFocusEnter();
                }
                break;
            }
            case QEvent::FocusOut:
            {
                if (m_fFocused && (pWatched == m_pButton1 ||
                                   pWatched == m_pButton2 ||
                                   pWatched == m_pButton3))
                {
                    m_fFocused = false;
                    emit sigFocusLeave();
                }
                break;
            }
            /* Default case: */
            default: break;
        }
    }
    /* Do not filter anything: */
    return false;
}

void UIPopupPane::showEvent(QShowEvent *pEvent)
{
    /* Make sure we should polish dialog: */
    if (m_fPolished)
        return;

    /* Call to polish-event: */
    polishEvent(pEvent);

    /* Mark dialog as polished: */
    m_fPolished = true;
}

void UIPopupPane::polishEvent(QShowEvent*)
{
    /* Adjust geometry: */
    adjustGeometry();
}

void UIPopupPane::keyPressEvent(QKeyEvent *pEvent)
{
    /* Preprocess Escape key: */
    if (pEvent->key() == Qt::Key_Escape && m_iButtonEsc)
    {
        done(m_iButtonEsc);
        return;
    }
    /* Handle all the other keys: */
    QWidget::keyPressEvent(pEvent);
}

int UIPopupPane::minimumWidthHint() const
{
    /* Prepare minimum width hint: */
    int iMinimumWidthHint = 0;

    /* Take into account main layout: */
    iMinimumWidthHint += 2 * m_iMainLayoutMargin;
    {
        /* Take into account main-frame layout: */
        iMinimumWidthHint += 2 * m_iMainFrameLayoutMargin;
        {
            /* Take into account widgets: */
            iMinimumWidthHint += m_pTextPane->width();
            iMinimumWidthHint += m_iMainFrameLayoutSpacing;
            iMinimumWidthHint += m_pButtonBox->minimumSizeHint().width();
        }
    }

    /* Return minimum width hint: */
    return iMinimumWidthHint;
}

int UIPopupPane::minimumHeightHint() const
{
    /* Prepare minimum height hint: */
    int iMinimumHeightHint = 0;

    /* Take into account main layout: */
    iMinimumHeightHint += 2 * m_iMainLayoutMargin;
    {
        /* Take into account main-frame layout: */
        iMinimumHeightHint += 2 * m_iMainFrameLayoutMargin;
        {
            /* Take into account widgets: */
            const int iTextPaneHeight = m_pTextPane->height();
            const int iButtonBoxHeight = m_pButtonBox->minimumSizeHint().height();
            iMinimumHeightHint += qMax(iTextPaneHeight, iButtonBoxHeight);
        }
    }

    /* Return minimum height hint: */
    return iMinimumHeightHint;
}

QSize UIPopupPane::minimumSizeHint() const
{
    return QSize(minimumWidthHint(), minimumHeightHint());
}

void UIPopupPane::adjustGeometry()
{
    /* Get parent width: */
    const int iWidth = parentWidget()->width();

    /* Adjust text-pane according parent width: */
    if (m_pTextPane)
    {
        m_pTextPane->setDesiredWidth(iWidth - 2 * m_iMainLayoutMargin
                                            - 2 * m_iMainFrameLayoutMargin
                                            - m_pButtonBox->minimumSizeHint().width());
    }

    /* Adjust other widgets: */
    sltAdjustGeomerty();
}

void UIPopupPane::updateLayout()
{
    /* This attributes: */
    const int iWidth = width();
    const int iHeight = height();
    /* Main layout: */
    {
        /* Main-frame: */
        m_pMainFrame->move(m_iMainLayoutMargin,
                           m_iMainLayoutMargin);
        m_pMainFrame->resize(iWidth - 2 * m_iMainLayoutMargin,
                             iHeight - 2 * m_iMainLayoutMargin);
        const int iMainFrameHeight = m_pMainFrame->height();
        /* Main-frame layout: */
        {
            /* Variables: */
            const int iTextPaneWidth = m_pTextPane->width();
            const int iTextPaneHeight = m_pTextPane->height();
            const QSize buttonBoxMinimumSizeHint = m_pButtonBox->minimumSizeHint();
            const int iButtonBoxWidth = buttonBoxMinimumSizeHint.width();
            const int iButtonBoxHeight = buttonBoxMinimumSizeHint.height();
            const int iMaximumHeight = qMax(iTextPaneHeight, iButtonBoxHeight);
            const int iMinimumHeight = qMin(iTextPaneHeight, iButtonBoxHeight);
            const int iHeightShift = (iMaximumHeight - iMinimumHeight) / 2;
            const bool fTextPaneShifted = iTextPaneHeight < iButtonBoxHeight;
            /* Text-pane: */
            m_pTextPane->move(m_iMainFrameLayoutMargin,
                              fTextPaneShifted ? m_iMainFrameLayoutMargin + iHeightShift : m_iMainFrameLayoutMargin);
            m_pTextPane->resize(iTextPaneWidth, iTextPaneHeight);
            /* Button-box: */
            m_pButtonBox->move(m_iMainFrameLayoutMargin + iTextPaneWidth + m_iMainFrameLayoutSpacing,
                               m_iMainFrameLayoutMargin);
            m_pButtonBox->resize(iButtonBoxWidth,
                                 iMainFrameHeight - 2 * m_iMainFrameLayoutMargin);
        }
    }
}

void UIPopupPane::prepareContent()
{
    /* Prepare this: */
    installEventFilter(this);
    /* Create main-frame: */
    m_pMainFrame = new UIPopupPaneFrame(this);
    {
        /* Prepare frame: */
        m_pMainFrame->installEventFilter(this);
        m_pMainFrame->setFocusPolicy(Qt::StrongFocus);
        /* Create message-label: */
        m_pTextPane = new UIPopupPaneTextPane(m_pMainFrame);
        {
            /* Prepare label: */
            connect(m_pTextPane, SIGNAL(sigSizeChanged()),
                    this, SLOT(sltAdjustGeomerty()));
            m_pTextPane->installEventFilter(this);
            m_pTextPane->setFocusPolicy(Qt::StrongFocus);
            m_pTextPane->setText(m_strMessage);
        }
        /* Create button-box: */
        m_pButtonBox = new QIDialogButtonBox(m_pMainFrame);
        {
            /* Prepare button-box: */
            m_pButtonBox->installEventFilter(this);
            m_pButtonBox->setOrientation(Qt::Vertical);
            prepareButtons();
        }
    }
}

void UIPopupPane::prepareButtons()
{
    /* Prepare descriptions: */
    QList<int> descriptions;
    descriptions << m_iButton1 << m_iButton2 << m_iButton3;

    /* Choose 'escape' button: */
    foreach (int iButton, descriptions)
        if (iButton & AlertButtonOption_Escape)
        {
            m_iButtonEsc = iButton & AlertButtonMask;
            break;
        }

    /* Create buttons: */
    QList<QPushButton*> buttons = createButtons(m_pButtonBox, descriptions);

    /* Install focus-proxy into the 'default' button: */
    foreach (QPushButton *pButton, buttons)
        if (pButton && pButton->isDefault())
        {
            m_pMainFrame->setFocusProxy(pButton);
            m_pTextPane->setFocusProxy(pButton);
            break;
        }

    /* Prepare button 1: */
    m_pButton1 = buttons[0];
    if (m_pButton1)
    {
        m_pButton1->installEventFilter(this);
        connect(m_pButton1, SIGNAL(clicked()), SLOT(done1()));
        if (!m_strButtonText1.isEmpty())
            m_pButton1->setText(m_strButtonText1);
    }
    /* Prepare button 2: */
    m_pButton2 = buttons[1];
    if (m_pButton2)
    {
        m_pButton2->installEventFilter(this);
        connect(m_pButton2, SIGNAL(clicked()), SLOT(done2()));
        if (!m_strButtonText2.isEmpty())
            m_pButton1->setText(m_strButtonText2);
    }
    /* Prepare button 3: */
    m_pButton3 = buttons[2];
    if (m_pButton3)
    {
        m_pButton3->installEventFilter(this);
        connect(m_pButton3, SIGNAL(clicked()), SLOT(done3()));
        if (!m_strButtonText3.isEmpty())
            m_pButton1->setText(m_strButtonText3);
    }
}

void UIPopupPane::done(int iButtonCode)
{
    /* Close the window: */
    close();

    /* Notify listeners: */
    emit sigDone(iButtonCode);
}

/* static */
int UIPopupPane::parentStatusBarHeight(QWidget *pParent)
{
    /* Check if passed parent is QMainWindow and contains status-bar: */
    if (QMainWindow *pParentWindow = qobject_cast<QMainWindow*>(pParent))
        if (pParentWindow->statusBar())
            return pParentWindow->statusBar()->height();
    /* Zero by default: */
    return 0;
}

/* static */
QList<QPushButton*> UIPopupPane::createButtons(QIDialogButtonBox *pButtonBox, const QList<int> descriptions)
{
    /* Create button according descriptions: */
    QList<QPushButton*> buttons;
    foreach (int iButton, descriptions)
        buttons << createButton(pButtonBox, iButton);
    /* Return buttons: */
    return buttons;
}

/* static */
QPushButton* UIPopupPane::createButton(QIDialogButtonBox *pButtonBox, int iButton)
{
    /* Null for AlertButton_NoButton: */
    if (iButton == 0)
        return 0;

    /* Prepare button text & role: */
    QString strText;
    QDialogButtonBox::ButtonRole role;
    switch (iButton & AlertButtonMask)
    {
        case AlertButton_Ok:      strText = QIMessageBox::tr("OK");     role = QDialogButtonBox::AcceptRole; break;
        case AlertButton_Cancel:  strText = QIMessageBox::tr("Cancel"); role = QDialogButtonBox::RejectRole; break;
        case AlertButton_Choice1: strText = QIMessageBox::tr("Yes");    role = QDialogButtonBox::YesRole; break;
        case AlertButton_Choice2: strText = QIMessageBox::tr("No");     role = QDialogButtonBox::NoRole; break;
        default: return 0;
    }

    /* Create push-button: */
    QPushButton *pButton = pButtonBox->addButton(strText, role);

    /* Configure button: */
    pButton->setFocusPolicy(Qt::StrongFocus);

    /* Configure 'default' button: */
    if (iButton & AlertButtonOption_Default)
        pButton->setDefault(true);

    /* Return button: */
    return pButton;
}


UIPopupPaneFrame::UIPopupPaneFrame(QWidget *pParent /*= 0*/)
    : QWidget(pParent)
    , m_iHoverAnimationDuration(300)
    , m_iDefaultOpacity(128)
    , m_iHoveredOpacity(230)
    , m_iOpacity(m_iDefaultOpacity)
{
    /* Prepare: */
    prepare();
}

UIPopupPaneFrame::~UIPopupPaneFrame()
{
    /* Cleanup: */
    cleanup();
}

void UIPopupPaneFrame::prepare()
{
    /* Propagate parent signals: */
    connect(parent(), SIGNAL(sigHoverEnter()), this, SIGNAL(sigHoverEnter()));
    connect(parent(), SIGNAL(sigHoverLeave()), this, SIGNAL(sigHoverLeave()));
    connect(parent(), SIGNAL(sigFocusEnter()), this, SIGNAL(sigFocusEnter()));
    connect(parent(), SIGNAL(sigFocusLeave()), this, SIGNAL(sigFocusLeave()));
    /* Install 'hover' animation for 'opacity' property: */
    UIAnimationFramework::installPropertyAnimation(this, QByteArray("opacity"),
                                                   m_iDefaultOpacity, m_iHoveredOpacity, m_iHoverAnimationDuration,
                                                   SIGNAL(sigHoverEnter()), SIGNAL(sigHoverLeave()));
}

void UIPopupPaneFrame::cleanup()
{
}

void UIPopupPaneFrame::paintEvent(QPaintEvent*)
{
    /* Compose painting rectangle: */
    const QRect rect(0, 0, width(), height());

    /* Create painter: */
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    /* Configure painter clipping: */
    QPainterPath path;
    int iDiameter = 6;
    QSizeF arcSize(2 * iDiameter, 2 * iDiameter);
    path.moveTo(iDiameter, 0);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-iDiameter, 0), 90, 90);
    path.lineTo(path.currentPosition().x(), rect.height() - iDiameter);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(0, -iDiameter), 180, 90);
    path.lineTo(rect.width() - iDiameter, path.currentPosition().y());
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-iDiameter, -2 * iDiameter), 270, 90);
    path.lineTo(path.currentPosition().x(), iDiameter);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-2 * iDiameter, -iDiameter), 0, 90);
    path.closeSubpath();
    painter.setClipPath(path);

    /* Fill with background: */
    QColor currentColor(palette().color(QPalette::Window));
    QColor newColor1(currentColor.red(), currentColor.green(), currentColor.blue(), opacity());
    QColor newColor2 = newColor1.darker(115);
    QLinearGradient headerGradient(rect.topLeft(), rect.topRight());
    headerGradient.setColorAt(0, newColor1);
    headerGradient.setColorAt(1, newColor2);
    painter.fillRect(rect, headerGradient);
}


UIPopupPaneTextPane::UIPopupPaneTextPane(QWidget *pParent /*= 0*/)
    : QWidget(pParent)
    , m_pLabel(0)
    , m_iDesiredWidth(-1)
    , m_fFocused(false)
    , m_pAnimation(0)
{
    /* Prepare: */
    prepare();
}

UIPopupPaneTextPane::~UIPopupPaneTextPane()
{
    /* Cleanup: */
    cleanup();
}

void UIPopupPaneTextPane::setText(const QString &strText)
{
    /* Make sure the text is changed: */
    if (m_pLabel->text() == strText)
        return;
    /* Update the pane for new text: */
    m_pLabel->setText(strText);
    updateGeometry();
}

void UIPopupPaneTextPane::setDesiredWidth(int iDesiredWidth)
{
    /* Make sure the desired-width is changed: */
    if (m_iDesiredWidth == iDesiredWidth)
        return;
    /* Update the pane for new desired-width: */
    m_iDesiredWidth = iDesiredWidth;
    updateGeometry();
}

QSize UIPopupPaneTextPane::minimumSizeHint() const
{
    /* Check if desired-width set: */
    if (m_iDesiredWidth >= 0)
        /* Return dependent size-hint: */
        return m_minimumSizeHint;
    /* Return golden-rule minimum size-hint by default: */
    return m_pLabel->minimumSizeHint();
}

QSize UIPopupPaneTextPane::sizeHint() const
{
    /* Check if desired-width set: */
    if (m_iDesiredWidth >= 0)
        /* Return dependent size-hint: */
        return m_sizeHint;
    /* Return golden-rule size-hint by default: */
    return m_pLabel->sizeHint();
}

void UIPopupPaneTextPane::sltFocusEnter()
{
    if (!m_fFocused)
    {
        m_fFocused = true;
        emit sigFocusEnter();
    }
}

void UIPopupPaneTextPane::sltFocusLeave()
{
    if (m_fFocused)
    {
        m_fFocused = false;
        emit sigFocusLeave();
    }
}

void UIPopupPaneTextPane::prepare()
{
    /* Propagate parent signals: */
    connect(parent(), SIGNAL(sigFocusEnter()), this, SLOT(sltFocusEnter()));
    connect(parent(), SIGNAL(sigFocusLeave()), this, SLOT(sltFocusLeave()));
    /* Prepare content: */
    prepareContent();
}

void UIPopupPaneTextPane::cleanup()
{
}

void UIPopupPaneTextPane::prepareContent()
{
    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    {
        /* Prepare layout: */
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->setSpacing(0);
        /* Create label: */
        m_pLabel = new QLabel;
        {
            /* Add into layout: */
            pMainLayout->addWidget(m_pLabel);
            /* Prepare label: */
            QFont currentFont = m_pLabel->font();
            currentFont.setPointSize(currentFont.pointSize() - 2);
            m_pLabel->setFont(currentFont);
            m_pLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            m_pLabel->setWordWrap(true);
        }
    }
}

void UIPopupPaneTextPane::updateGeometry()
{
    /* Recalculate size-hints: */
    QFontMetrics fm(m_pLabel->font(), m_pLabel);
    m_sizeHint = QSize(m_iDesiredWidth, m_pLabel->heightForWidth(m_iDesiredWidth));
    m_minimumSizeHint = QSize(m_iDesiredWidth, fm.height());
    /* Update geometry: */
    QWidget::updateGeometry();
    /* Resize to required size: */
    resize(m_fFocused ? sizeHint() : minimumSizeHint());
    /* Reinstall animation: */
    delete m_pAnimation;
    m_pAnimation = UIAnimationFramework::installPropertyAnimation(this, "size", "minimumSizeHint", "sizeHint",
                                                                  SIGNAL(sigFocusEnter()), SIGNAL(sigFocusLeave()),
                                                                  m_fFocused);
}

void UIPopupPaneTextPane::resizeEvent(QResizeEvent*)
{
    emit sigSizeChanged();
}

#include "UIPopupPane.moc"

